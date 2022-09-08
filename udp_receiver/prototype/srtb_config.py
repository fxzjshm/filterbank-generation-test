# parameters and requirements from backend
acc=499

# sigproc filterbank header (http://sigproc.sourceforge.net/sigproc.pdf) and related
# dummy values, need to be edited
## source
source_name = "sun"  # the name of the source observed
src_raj =   53432.0  # source location in hhmmss.s
src_dej = +220052.2  # source location in ddmmss.s

## file
nsamples = 1000000  # target nsamples for a file, take care of memory
filename_prefix = source_name
data_location = ""  # the dir to save received data to, leave this blank to use current location.

## FFT
### ** foff < 0 so that `dedisperse` can work. If foff > 0, set fch1 and reverse_channel accordingly **
fch1 = 1500.0 - 500.0/1024.0  # frequency of the first channel
foff = -500.0/1024.0  # \delta f between two channels
nchans = 1024  # channel count, should be checked during runtime
tsamp = 0.000001*2.048*(acc+1)  # time interval for a sample
data_type = 1  # filterbank data = 1, time series = 2

deinterlace_channel = True
reverse_channel = True

## telescope
telescope_id = 7
machine_id = 255
nbeams = 1
nbits = 8  # data type
nifs = 2  # polar directions

## dummy
rawdatafile = "test.fil"  # replaced by generated name
tstart = 59728.04167  # start time of observe, should be auto updated

# udp
MCAST_GRP = '10.0.1.2'
MCAST_PORT = 12001
BUFFER_SIZE = 10240

# misc
# it is said that in formal observe the udp pack counter (first 8 bytes of a packet, in `uint64`` or `unsigned long long`) should start with 0
start_from_counter_zero = False

# srtb = simple radio telescope backend
