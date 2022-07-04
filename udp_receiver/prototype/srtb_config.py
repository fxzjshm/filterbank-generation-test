# sigproc filterbank header, see http://sigproc.sourceforge.net/sigproc.pdf
# dummy values, need to be edited
telescope_id = 255
machine_id = 255
rawdatafile = "test.fil"  # replaced by generated name
source_name = "crab"  # the name of the source observed
data_type = 1  # filterbank data = 1, time series = 2
fch1 = 1500.0  # frequency of the first channel
foff = -500.0/1024.0  # \delta f between two channels
nchans = 1024  # channel count, should be checked during runtime
tsamp = 0.001  # time interval for a sample
nbeams = 1
nbits = 8  # data type
nifs = 2  # polar directions
src_raj =  053432.0  # source location in hhmmss.s
src_dej = +220052.2  # source location in ddmmss.s
tstart = 59728.04167  # start time of observe, should be auto updated
nsamples = 500000  # target nsamples for a file, take care of memory

# udp
MCAST_GRP = '10.0.1.2'
MCAST_PORT = 12001
BUFFER_SIZE = 10240

# misc
filename_prefix = "crab"
# it is said that in formal observe the udp pack counter (first 8 bytes of a packet, in `uint64`` or `unsigned long long`) should start with 0
start_from_counter_zero = False
deinterlace_channel = True
