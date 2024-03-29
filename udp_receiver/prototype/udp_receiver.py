#!/usr/bin/env python3

################################################################################
# Copyright (c) 2022 fxzjshm
# This software is licensed under Mulan PubL v2.
# You can use this software according to the terms and conditions of the Mulan PubL v2.
# You may obtain a copy of Mulan PubL v2 at:
#          http://license.coscl.org.cn/MulanPubL-2.0
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PubL v2 for more details.
################################################################################

# sigproc writer from PRESTO (https://github.com/scottransom/presto) is used
import __presto.sigproc as sigproc
import astropy.time
import time
# UDP multicast receive ref: https://stackoverflow.com/questions/603852/how-do-you-udp-multicast-in-python
import socket
import struct
import numpy as np

import srtb_config

def generate_filterbank_header():
    """
    Use presto.sigproc to generate .fil header, 
    configs are in srtb_config
    reference: sigproc/filterbank_header.c
    """
    header = bytearray()
    header += sigproc.addto_hdr("HEADER_START", None)
    header += sigproc.addto_hdr("telescope_id", srtb_config.telescope_id)
    header += sigproc.addto_hdr("machine_id", srtb_config.machine_id)
    header += sigproc.addto_hdr("rawdatafile", srtb_config.rawdatafile)
    header += sigproc.addto_hdr("source_name", srtb_config.source_name)
    header += sigproc.addto_hdr("data_type", srtb_config.data_type)
    header += sigproc.addto_hdr("fch1", srtb_config.fch1)
    header += sigproc.addto_hdr("foff", srtb_config.foff)
    header += sigproc.addto_hdr("nchans", srtb_config.nchans)
    header += sigproc.addto_hdr("tsamp", srtb_config.tsamp)
    header += sigproc.addto_hdr("nbeams", srtb_config.nbeams)
    header += sigproc.addto_hdr("nbits", srtb_config.nbits)
    header += sigproc.addto_hdr("nifs", srtb_config.nifs)
    header += sigproc.addto_hdr("src_raj", srtb_config.src_raj)
    header += sigproc.addto_hdr("src_dej", srtb_config.src_dej)
    header += sigproc.addto_hdr("tstart", srtb_config.tstart)
    header += sigproc.addto_hdr("nsamples", srtb_config.nsamples)
    header += sigproc.addto_hdr("HEADER_END", None)
    return header

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # on this port, listen ONLY to MCAST_GRP
    sock.bind((srtb_config.MCAST_GRP, srtb_config.MCAST_PORT))

    counter = -1  # last read counter, i.e. condition: data_counter == counter + 1:

    while True:
        # generate tstart, file_name
        srtb_config.tstart = astropy.time.Time(time.time(), format="unix").mjd
        file_name = srtb_config.filename_prefix + str("_{:.8f}.fil").format(srtb_config.tstart)
        print(f"[INFO] receiving to {file_name}") 
        srtb_config.rawdatafile = file_name

        # open file handle
        outfile = open(srtb_config.data_location + file_name, 'wb')
        header = generate_filterbank_header()
        outfile.write(header)

        nsamples = 0
        data = bytes()  # received udp data

        expected_data_length = srtb_config.nifs * srtb_config.nchans * srtb_config.nbits / 8
        if srtb_config.sum_ifs == True:
            expected_data_length *= 2

        while nsamples < srtb_config.nsamples:
            data_counter = -1  # counter read from udp packet
            while True:
                data = sock.recv(srtb_config.BUFFER_SIZE)
                # data structure:
                #     xxxxxxxxxxxxxx......xxxxxx  <-- one sample
                #     |------||-----......-----|
                #      counter `nchan` channels
                #      8 bytes  nchan * nbits/8 bytes
                data_counter = struct.unpack("Q", data[:8])[0]
                # break this loop if don't need to start from 0, or have started reading, or has read 0
                if (not srtb_config.start_from_counter_zero) or (nsamples != 0) or (data_counter == 0):
                    break
            data_content = data[8:]
            data_length = len(data_content)
            
            if data_length != expected_data_length :
                print(f"[WARNING] length mismatch, received length = {data_length}, expected nchan = {srtb_config.nchans}, nbits = {srtb_config.nbits}, ignoring.")
                continue
            if data_counter != counter + 1:
                print(f"[WARNING] data loss detected: skipping {data_counter - counter - 1} packets.")
            nsamples += 1
            counter = data_counter
            #print(f"[DEBUG] nsamples = {nsamples}")
            
            # input data may be interlaced:
            #         if(1)ch(1)  if(2)ch(1)  if(1)ch(2)  if(2)ch(2)  ...  if(1)ch(nchans)  if(2)ch(nchans)
            # choices:
            ## 1) one needs summed/averaged results, i.e. 
            ##         ((if(1)+if(2))/2)ch(1)  ((if(1)+if(2))/2)ch(2)  ...  ((if(1)+if(2))/2)ch(nchans)
            ## 2) one needs two polarizations, however sigproc .fil requires:
            ##         if(1)ch(1)  if(1)ch(2)  ...  if(1)ch(nchans)  if(2)ch(1)  if(2)ch(2)  ...  if(2)ch(nchans)
            ## deinterlace is therefore required.
            ## 3) do not do extra process
            # moreover, `dedisperse`'s algorithm requires foff < 0, but input often has foff > 0, so need to reverse channels.
            if srtb_config.sum_ifs == True:
                if srtb_config.nbits == 8:
                    # average every two bytes
                    # ref: https://stackoverflow.com/questions/15956309/averaging-over-every-n-elements-of-a-numpy-array
                    #      https://stackoverflow.com/questions/47637758/how-can-i-make-a-numpy-ndarray-from-bytes
                    #outfile.write(bytes([round((a + b) / 2) for a, b in zip(data_content[::2], data_content[1::2])]))
                    np_array = np.frombuffer(data_content, dtype=np.uint8)
                    np_meaned = np.round(np.mean(np_array.reshape(-1, 2), axis=1))
                    data_meaned = np_meaned.astype(np.uint8).tobytes()
                    if srtb_config.reverse_channel == True:
                        outfile.write(data_meaned[::-1])
                    else:
                        outfile.write(data_meaned)
                else:
                    raise Exception("sum_ifs: TODO: nbits == 1, 2, 4 and reverse")
            elif srtb_config.nifs == 2 and srtb_config.deinterlace_channel == True:
                if srtb_config.nbits == 8:
                    if srtb_config.reverse_channel == True:
                        outfile.write(data_content[::-2])
                        outfile.write(data_content[-2::-2])
                    else:
                        outfile.write(data_content[0::2])
                        outfile.write(data_content[1::2])
                else:
                    raise Exception("deinterlace_channel: TODO: nbits == 1, 2, 4 and reverse")
            else:
                if srtb_config.reverse_channel == True:
                    outfile.write(data_content[::-1])
                else:
                    outfile.write(data_content)
        
        outfile.close()


if __name__ == "__main__":
    main()
