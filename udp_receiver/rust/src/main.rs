/*******************************************************************************
 * Copyright (c) 2022-2023 fxzjshm
 * This software is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 ******************************************************************************/

#![allow(non_camel_case_types)]
#![allow(unused_parens)]
#![allow(non_upper_case_globals)]

use chrono;
use pyo3;
use pyo3::prelude::*;
use sigproc_filterbank;
use std;
use std::io::Write;

type udp_receiver_counter_type = u64;

fn main() {
  // read config file, pyo3 used for compatibility
  // TODO: use a template method to read config, but unlike C++, some trait requirements is not satisfied
  let current_dir = std::env::current_dir().unwrap().display().to_string();
  let gil = Python::acquire_gil();
  let py = gil.python();
  let import_code = format!("sys.path.append(\"{}\")", current_dir);
  py.run("import sys", None, None).unwrap();
  py.run(import_code.as_str(), None, None).unwrap();
  let srtb_config = PyModule::import(py, "srtb_config").expect("Cannot evaluate srtb_config");

  // bind to address
  let udp_ip: String = srtb_config.getattr("MCAST_GRP").unwrap().extract().unwrap();
  let udp_port: u16 = srtb_config.getattr("MCAST_PORT").unwrap().extract().unwrap();
  let udp_address = format!("{udp_ip}:{udp_port}");
  println!("[main] binding UDP socket to {udp_address}");
  let socket = std::net::UdpSocket::bind(udp_address).unwrap();

  // header parameters
  let telescope_id: u32 = srtb_config.getattr("telescope_id").unwrap().extract().unwrap();
  let machine_id: u32 = srtb_config.getattr("machine_id").unwrap().extract().unwrap();
  let source_name: String = srtb_config.getattr("source_name").unwrap().extract().unwrap();
  let data_type: u32 = srtb_config.getattr("data_type").unwrap().extract().unwrap();
  let fch1: f64 = srtb_config.getattr("fch1").unwrap().extract().unwrap();
  let foff: f64 = srtb_config.getattr("foff").unwrap().extract().unwrap();
  let nchans: usize = srtb_config.getattr("nchans").unwrap().extract().unwrap();
  let tsamp: f64 = srtb_config.getattr("tsamp").unwrap().extract().unwrap();
  let nbeams: usize = srtb_config.getattr("nbeams").unwrap().extract().unwrap();
  let nbits: usize = srtb_config.getattr("nbits").unwrap().extract().unwrap();
  let nifs: usize = srtb_config.getattr("nifs").unwrap().extract().unwrap();
  let src_raj: f64 = srtb_config.getattr("src_raj").unwrap().extract().unwrap();
  let src_dej: f64 = srtb_config.getattr("src_dej").unwrap().extract().unwrap();

  // other parameters
  let target_nsamples: u32 = srtb_config.getattr("nsamples").unwrap().extract().unwrap();
  let file_name_prefix: String = srtb_config.getattr("filename_prefix").unwrap().extract().unwrap();
  let data_location: String = srtb_config.getattr("data_location").unwrap().extract().unwrap();
  let sum_ifs: bool = srtb_config.getattr("sum_ifs").unwrap().extract().unwrap();
  let expected_written_data_length = nifs * nchans * nbits / 8;
  let expected_received_data_length = expected_written_data_length * (if (sum_ifs) { 2 } else { 1 });
  let start_from_counter_zero: bool = srtb_config
    .getattr("start_from_counter_zero")
    .unwrap()
    .extract()
    .unwrap();
  let reverse_channel: bool = srtb_config.getattr("reverse_channel").unwrap().extract().unwrap();
  let deinterlace_channel: bool = srtb_config.getattr("deinterlace_channel").unwrap().extract().unwrap();
  const udp_packet_max_size: usize = 65536;
  let mut packet_buffer = [0 as u8; udp_packet_max_size];
  let mut output_buffer = [0 as u8; udp_packet_max_size];
  let zeros_buffer = [0 as u8; udp_packet_max_size];
  const udp_receiver_counter_size: usize = std::mem::size_of::<udp_receiver_counter_type>();

  // check parameters, some are not supported
  assert_eq!(nbits, 8, "only nbits == 8 is supported by this version");
  assert_eq!(start_from_counter_zero, false, "not supporting start_from_counter_zero");

  // counter is used across files
  let mut last_counter: udp_receiver_counter_type = 0;
  let mut last_counter_set = false;

  // main loop
  loop {
    // mjd
    const seconds_of_a_day: u32 = 24 * 60 * 60;
    let utc_now = chrono::Utc::now();
    let mjd_time = utc_now
      .time()
      .signed_duration_since(chrono::NaiveTime::from_hms_opt(0, 0, 0).unwrap())
      .to_std()
      .unwrap()
      .as_secs_f64()
      / (seconds_of_a_day as f64);
    let mjd_date = julianday::ModifiedJulianDay::from(utc_now.date_naive()).inner();
    let mjd = (mjd_date as f64) + mjd_time;
    println!("[main] mjd = {}", mjd);
    let tstart = mjd;

    // open file handle
    let file_name = format!("{}_{:.8}.fil", file_name_prefix, mjd);
    let file_path = format!("{}{}", data_location, file_name);
    println!("[main] receiving to {}", file_path);
    let mut file = std::fs::File::create(file_path).unwrap();

    // filterbank
    let mut filterbank: sigproc_filterbank::write::WriteFilterbank<u8> =
      sigproc_filterbank::write::WriteFilterbank::new(nchans, nifs);
    let rawdatafile = file_name;
    filterbank.telescope_id = Some(telescope_id);
    filterbank.machine_id = Some(machine_id);
    filterbank.rawdatafile = Some(rawdatafile);
    filterbank.source_name = Some(source_name);
    filterbank.data_type = Some(data_type);
    filterbank.fch1 = Some(fch1);
    filterbank.foff = Some(foff);
    //filterbank.nchans = Some(nchans);
    filterbank.tsamp = Some(tsamp);
    filterbank.nbeams = Some(nbeams);
    //filterbank.nbits = Some(nbits);
    //filterbank.nifs = Some(nifs);
    filterbank.src_raj = Some(src_raj);
    filterbank.src_dej = Some(src_dej);
    filterbank.tstart = Some(tstart);
    //filterbank.nsamples = Some(nsamples);

    let mut nsamples = 0;
    while (nsamples < target_nsamples) {
      // receive packet & check length
      let data_length = socket.recv(&mut packet_buffer).unwrap() - udp_receiver_counter_size;
      if (data_length != expected_received_data_length) {
        println!(
          "[main] warning: length mismatch, received = {data_length}, expected = {expected_received_data_length}"
        );
        continue;
      }
      let output_length = expected_written_data_length;
      assert!(output_length <= output_buffer.len());

      // data structure:
      //     xxxxxxxxxxxxxx......xxxxxx  <-- one sample
      //     |------||-----......-----|
      //      counter `nchan` channels
      //      8 bytes  nchan * nbits/8 bytes

      // re-construct packet counter and check
      const bits_per_byte: usize = 8;
      let mut data_counter: udp_receiver_counter_type = 0;
      for i in 0..udp_receiver_counter_size {
        data_counter |= ((packet_buffer[i] as udp_receiver_counter_type) << (bits_per_byte * i));
      }
      let lost_packets = data_counter - last_counter - 1;
      if (lost_packets != 0) {
        if (last_counter_set == false) {
          //last_counter = data_counter;
          last_counter_set = true;
        } else {
          println!(
            "[main] warning: data loss detected: skipped {} packets. Filling with 0",
            lost_packets
          );
          for _ in 0..lost_packets {
            filterbank.push(&zeros_buffer[0..output_length]);
          }
        }
      }
      nsamples += 1;
      last_counter = data_counter;

      // input data may be interlaced:
      //         if(1)ch(1)  if(2)ch(1)  if(1)ch(2)  if(2)ch(2)  ...  if(1)ch(nchans)  if(2)ch(nchans)
      // choices:
      //  1) one needs summed/averaged results, i.e.
      //          ((if(1)+if(2))/2)ch(1)  ((if(1)+if(2))/2)ch(2)  ...  ((if(1)+if(2))/2)ch(nchans)
      //  2) one needs two polarizations, however sigproc .fil requires:
      //          if(1)ch(1)  if(1)ch(2)  ...  if(1)ch(nchans)  if(2)ch(1)  if(2)ch(2)  ...  if(2)ch(nchans)
      //  deinterlace is therefore required.
      //  3) do not do extra process
      // moreover, `dedisperse`'s algorithm requires foff < 0, but input often has foff > 0, so need to reverse channels.
      let offset = udp_receiver_counter_size;
      if (sum_ifs == true) {
        if (nbits == 8) {
          // average every two bytes

          // manually check boundaries
          // the compiler isn't smart enough
          assert!(offset + 2 * output_length <= packet_buffer.len());
          assert!(output_length <= output_buffer.len());
          if (reverse_channel) {
            for i in 0..output_length {
              //output_buffer[output_length - i - 1] =
              //  ((packet_buffer[offset + 2 * i] / 2) + (packet_buffer[offset + 2 * i + 1] / 2));

              // rustc, listen, you must generate SIMD instructions, make use of ymm registers on amd64, do you understand ?!
              unsafe {
                *output_buffer.get_unchecked_mut(output_length - i - 1) =
                  ((*packet_buffer.get_unchecked(offset + 2 * i)) / 2
                    + (*packet_buffer.get_unchecked(offset + 2 * i + 1) / 2));
              }
              // verify: (with debug info)
              // cargo objdump --release -- -S -C -d > /tmp/filterbank_udp_receiver.txt
            }
          } else {
            for i in 0..output_length {
              //output_buffer[i] = ((packet_buffer[offset + 2 * i] / 2) + (packet_buffer[offset + 2 * i + 1] / 2));
              unsafe {
                *output_buffer.get_unchecked_mut(i) = ((*packet_buffer.get_unchecked(offset + 2 * i) / 2)
                  + (*packet_buffer.get_unchecked(offset + 2 * i + 1) / 2));
              }
            }
          }
        } else {
          panic!("[main] sum_ifs: TODO: nbits == 1, 2, 4 and reverse");
        }
      } else if (nifs == 2 && deinterlace_channel == true) {
        if (nbits == 8) {
          assert!(output_length <= output_buffer.len());
          assert!(offset + output_length <= packet_buffer.len());
          if (reverse_channel == true) {
            for i in 0..(output_length / 2) {
              //output_buffer[(output_length / 2) - 1 - i] = packet_buffer[offset + 2 * i];
              //output_buffer[output_length - 1 - i] = packet_buffer[offset + 2 * i + 1];
              unsafe {
                *output_buffer.get_unchecked_mut((output_length / 2) - 1 - i) =
                  *packet_buffer.get_unchecked(offset + 2 * i);
                *output_buffer.get_unchecked_mut(output_length - 1 - i) =
                  *packet_buffer.get_unchecked(offset + 2 * i + 1)
              }
            }
          } else {
            for i in 0..(output_length / 2) {
              //output_buffer[i] = packet_buffer[offset + 2 * i];
              //output_buffer[i + (output_length / 2)] = packet_buffer[offset + 2 * i + 1];
              unsafe {
                *output_buffer.get_unchecked_mut(i) = *packet_buffer.get_unchecked(offset + 2 * i);
                *output_buffer.get_unchecked_mut(i + (output_length / 2)) =
                  *packet_buffer.get_unchecked(offset + 2 * i + 1)
              }
            }
          }
        } else {
          panic!("[main] deinterlace_channel: TODO: nbits == 1, 2, 4 and reverse");
        }
      } else {
        assert!(output_length <= output_buffer.len());
        assert!(offset + output_length <= packet_buffer.len());
        if (reverse_channel == true) {
          for i in 0..output_length {
            //output_buffer[output_length - 1 - i] = packet_buffer[offset + i];
            unsafe {
              *output_buffer.get_unchecked_mut(output_length - 1 - i) = *packet_buffer.get_unchecked(offset + i);
            }
          }
        } else {
          for i in 0..output_length {
            //output_buffer[i] = packet_buffer[offset + i];
            unsafe {
              *output_buffer.get_unchecked_mut(i) = *packet_buffer.get_unchecked(offset + i);
            }
          }
        }
      }

      filterbank.push(&output_buffer[0..output_length]);
    }

    file.write(&filterbank.bytes()).unwrap();

    break;
  }

  //
}
