/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#define BOOST_COMPUTE_DEBUG_KERNEL_COMPILATION

#include "stopwatch.h"
#include <boost/compute.hpp>
#include <boost/program_options.hpp>
#include <clFFT.h>
#include <filesystem>
#include <fstream>
#include <iostream>

#define HD_BENCHMARK

#ifdef HD_BENCHMARK
void start_timer(Stopwatch &timer) { timer.start(); }
void stop_timer(Stopwatch &timer, boost::compute::command_queue &queue = boost::compute::system::default_queue()) {
    queue.finish();
    timer.stop();
}
#else
void start_timer(Stopwatch &timer) {}
void stop_timer(Stopwatch &timer, boost::compute::command_queue &queue = boost::compute::system::default_queue()) {}
#endif // HD_BENCHMARK

typedef float data_type;

std::string kernel_source = BOOST_COMPUTE_STRINGIZE_SOURCE(
    __kernel void generate(__global data_type *d_in, ulong nsamp /*, data_type dt*/) {
        size_t i = get_global_id(0);
        /*
        data_type t = dt * ((data_type)i);
        data_type x = t / 400.0f;
        d_in[i] = sin(2*M_PI_F*40*x) + ((data_type)0.5) * sin(2*M_PI_F*90*x);
        */
        uint m = 0xCAFEBABEDEADCAFE; //
        uint t = i & 0x3F;
        for (uint i = 0; i < t; i++) {
            m = (m << 1) | (m >> 63);
        }
        d_in[i] = m / 1e18;
    }

    __kernel void normalize_complex_number(__global data_type *d_out_complex, __global data_type *d_out_real) {
        size_t i = get_global_id(0);
        d_out_real[i] = (sqrt(d_out_complex[2 * i] * d_out_complex[2 * i] + d_out_complex[2 * i + 1] * d_out_complex[2 * i + 1]));
    });

template <typename T>
void write_vector(std::vector<T> &vec, size_t width, size_t height, std::string name) {
    std::ofstream file(name);
    for (size_t i = 0; i < height; i++) {
        file << vec[i * width];
        for (size_t j = 1; j < width; j++) {
            file << " " << vec[i * width + j];
        }
        // file << " ";
        file << std::endl;
    }
    file.close();
}

int main(int argc, char **argv) {
    std::ios::sync_with_stdio(false);

    // ------------
    // Parse arguments & show help
    // ------------
    boost::program_options::options_description option_desc("Options");
    using boost::program_options::value;
    /* clang-format off */
    option_desc.add_options()
        ("help,h", "Show help message")
        ("nsamp_seg", value<size_t>(), "Number of points to be FFT-ed in one segment")
        ("seg_count", value<size_t>(), "Number of segments of points to be FFT-ed at one kernel call")
        ("input_file,f", value<std::string>(), "Input file")
        ("output_file,o", value<std::string>(), "Output file")
        ("sample_rate", value<float>(), "Sample rate of input time series")
        ("fmin", value<float>(), "Min of frequency of output channel")
        ("fmax", value<float>(), "Max of frequency of output channel")
        ("text", "Read input file as text")
    ;
    boost::program_options::positional_options_description p;
    p.add("input_file", 1);
    /* clang-format on */
    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(option_desc).positional(p).run(), vm);
    boost::program_options::notify(vm);

    if (vm.count("help") || (!(vm.count("nsamp_seg") && vm.count("seg_count") && vm.count("input_file") && vm.count("sample_rate")))) {
        std::cout << option_desc << "\n";
        return 1;
    }

    // ------------

    // ------------
    // Read arguments and set up host side
    // ------------
    size_t in_nsamp_seg = vm["nsamp_seg"].as<size_t>();
    size_t seg_count = vm["seg_count"].as<size_t>();
    size_t nsamp = in_nsamp_seg * seg_count;
    size_t out_nsamp_seg = 1 + in_nsamp_seg / 2;
    size_t out_nsamp = out_nsamp_seg * seg_count;
    float sample_rate = vm["sample_rate"].as<float>();
    float df = sample_rate / in_nsamp_seg;
    float fmin = 0.0f;
    if (vm.count("fmin")) {
        fmin = vm["fmin"].as<float>();
    }
    float fmax;
    if (vm.count("fmax")) {
        fmax = vm["fmax"].as<float>();
    } else {
        fmax = (in_nsamp_seg / 2) * df;
    }
    size_t fmin_id = std::max(static_cast<size_t>(std::round(fmin / df)), static_cast<size_t>(0));
    size_t fmax_id = std::min(static_cast<size_t>(std::round(fmax / df)), static_cast<size_t>(out_nsamp_seg - 1));
    if (fmin_id > fmax_id) {
        std::cerr << "fmin_id = " << fmin_id << "but max fmax_id = " << fmax_id << std::endl;
        exit(-1);
    }
    size_t out_part_nsamp_seg = (fmax_id - fmin_id + 1);
    size_t out_part_nsamp = out_part_nsamp_seg * seg_count;
    std::string in_file_name = vm["input_file"].as<std::string>();

    size_t in_file_nsamps;
    std::vector<data_type> h_in;
    if (vm.count("text")) {
        std::ifstream in_file_stream(in_file_name);
        h_in = std::vector<data_type>(std::istream_iterator<data_type>(in_file_stream), {});
        in_file_nsamps = h_in.size();
    } else {
        FILE *in_file_stream;
        in_file_stream = fopen(in_file_name.c_str(), "rb");
        size_t in_file_length = std::filesystem::file_size(in_file_name);
        in_file_nsamps = in_file_length / sizeof(data_type);
        h_in.resize(in_file_nsamps);
        fread(&h_in[0], 1, in_file_length, in_file_stream);
        fclose(in_file_stream);
    }
    size_t iteration = in_file_nsamps / nsamp;
    size_t out_file_nsamps = out_nsamp * iteration;
    size_t out_part_file_nsamps = out_part_nsamp * iteration;
    std::vector<data_type> h_out_complex(2 * out_file_nsamps), h_out_real(out_file_nsamps);
    std::vector<data_type> h_out_part(out_part_file_nsamps);
    // ------------

    // ------------
    // Print info
    // ------------
    std::cout << "nsamp_seg = " << in_nsamp_seg << std::endl
              << "seg_count = " << seg_count << std::endl
              << "nsamp = " << nsamp << std::endl
              << "in_file_name = " << in_file_name << std::endl
              << "in_file_nsamps = " << in_file_nsamps << std::endl
              << "iteration = " << iteration << std::endl
              << "fmin_id = " << fmin_id << std::endl
              << "fmax_id = " << fmax_id << std::endl;

    namespace bc = boost::compute;
    bc::command_queue queue = bc::system::default_queue();
    bc::context context = queue.get_context();
    bc::device device = queue.get_device();
    std::cout << "Using device " << device.name() << " on platform " << device.platform().name() << std::endl;
    // ------------

    Stopwatch setup_timer, generate_timer, fft_timer, normalize_timer, copy_timer, write_timer;

    // ------------
    // Set up device side
    // ------------
    start_timer(setup_timer);
    clfftSetupData clfft_setup_data;
    clfftInitSetupData(&clfft_setup_data);
    clfftSetup(&clfft_setup_data);
    clfftPlanHandle plan_handle;
    clfftDim dim = CLFFT_1D;
    size_t cl_lengths[1] = {in_nsamp_seg};
    clfftCreateDefaultPlan(&plan_handle, context.get(), dim, cl_lengths);
    clfftSetPlanPrecision(plan_handle, CLFFT_SINGLE);
    clfftSetLayout(plan_handle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
    clfftSetResultLocation(plan_handle, CLFFT_OUTOFPLACE);
    clfftSetPlanBatchSize(plan_handle, seg_count);
    clfftSetPlanDistance(plan_handle, in_nsamp_seg, out_nsamp_seg);
    clfftBakePlan(plan_handle, 1, &(queue.get()), NULL, NULL);

    bc::vector<data_type> d_in(nsamp), d_out_complex(2 * out_nsamp), d_out_real(out_nsamp);
    bc::program program = bc::program::build_with_source(kernel_source, context, std::string("-Ddata_type=") + bc::type_name<data_type>());
    //bc::kernel generate_kernel(program, "generate");
    bc::kernel normalize_kernel(program, "normalize_complex_number");
    stop_timer(setup_timer);
    std::cout << "setup_timer: " << setup_timer.getTime() << std::endl;
    std::cout << "d_in size : " << d_in.get_buffer().get_memory_size() << " bytes" << std::endl;
    // ------------

    /*
    // ------------
    start_timer(generate_timer);
    generate_kernel.set_args(d_in.get_buffer().get(), static_cast<cl_ulong>(nsamp));
    queue.enqueue_1d_range_kernel(generate_kernel, 0, nsamp, 0);
    stop_timer(generate_timer);
    std::cout << "generate_timer: " << generate_timer.getTime() << std::endl;
    // ------------
*/

    // ------------
    // Do generation
    // ------------
    size_t in_offset = 0, out_offset = 0, out_part_offset = 0;
    for (size_t i = 0; i < iteration; i++) {
        start_timer(copy_timer);
        bc::copy(h_in.begin() + in_offset, h_in.begin() + in_offset + nsamp, d_in.begin());
        stop_timer(copy_timer);

        start_timer(fft_timer);
        clfftEnqueueTransform(plan_handle, CLFFT_FORWARD, 1, &(queue.get()), 0, NULL, NULL, &(d_in.get_buffer().get()), &(d_out_complex.get_buffer().get()), NULL);
        stop_timer(fft_timer);

        start_timer(normalize_timer);
        normalize_kernel.set_args(d_out_complex.get_buffer().get(), d_out_real.get_buffer().get());
        queue.enqueue_1d_range_kernel(normalize_kernel, 0, out_nsamp, 0);
        stop_timer(normalize_timer);

        start_timer(copy_timer);
        bc::copy(d_in.begin(), d_in.end(), h_in.begin() + in_offset);
        bc::copy(d_out_complex.begin(), d_out_complex.end(), h_out_complex.begin() + 2 * out_offset);
        bc::copy(d_out_real.begin(), d_out_real.end(), h_out_real.begin() + out_offset);
        // queue.enqueue_read_buffer_rect(d_out_real.get_buffer(),
        //                                /* buffer_offset = */ bc::dim(fmin_id, 0, 0).data(),
        //                                /* host_offset = */ bc::dim(fmin_id, seg_count * i, 0).data(),
        //                                /* region = */ bc::dim(out_part_nsamp_seg, seg_count, 1).data(),
        //                                /* buffer_row_pitch = */ out_nsamp_seg,
        //                                /* buffer_slice_pitch = */ 0,
        //                                /* host_row_pitch = */ out_part_nsamp_seg,
        //                                /* host_slice_pitch = */ 0,
        //                                &h_out_part[0]);
        stop_timer(copy_timer);

        std::cout << "fft_timer: " << fft_timer.getTime() << "  "
                  << "normalize_timer: " << normalize_timer.getTime() << "  "
                  << "copy_timer: " << copy_timer.getTime()
                  << "    \r";
        std::cout.flush();

        in_offset += nsamp;
        out_offset += out_nsamp;
        out_part_offset += out_part_nsamp;
    }
    std::cout << "\nfft_timer (average): " << fft_timer.getAverageTime() << " ms" << std::endl;
    // ------------

    clfftDestroyPlan(&plan_handle);
    clfftTeardown();

    start_timer(copy_timer);
    for (size_t i = 0; i < seg_count * iteration; i++) {
        for (size_t j = 0; j < out_part_nsamp_seg; j++) {
            assert(out_part_nsamp_seg * i + j < h_out_part.size());
            assert(out_nsamp_seg * i + (fmax_id - j) < h_out_real.size());
            h_out_part[out_part_nsamp_seg * i + j] = h_out_real[out_nsamp_seg * i + (fmax_id - j)];
        }
        // write_vector(h_out_part, out_part_nsamp_seg, seg_count * iteration, std::string("fil-test-part") + std::to_string(i) + ".txt");
    }
    stop_timer(copy_timer);

    // ------------
    start_timer(write_timer);
    write_vector(h_in, in_file_nsamps, 1, "fil-test-in-1d.txt");
    write_vector(h_in, in_nsamp_seg, seg_count * iteration, "fil-test-in-2d.txt");
    write_vector(h_out_complex, 2 * out_nsamp_seg, seg_count * iteration, "fil-test-complex.txt");
    write_vector(h_out_real, out_nsamp_seg, seg_count * iteration, "fil-test-real.txt");
    write_vector(h_out_part, out_part_nsamp_seg, seg_count * iteration, "fil-test-part.txt");
    stop_timer(write_timer);
    std::cout << "write_timer: " << write_timer.getTime() << std::endl;
    // ------------

    return 0;
}
