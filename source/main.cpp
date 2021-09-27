/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#define BOOST_COMPUTE_DEBUG_KERNEL_COMPILATION
#define HD_BENCHMARK

#include <boost/compute.hpp>
#include <boost/program_options.hpp>
#include <clFFT.h>
#include <filesystem>
#include <fstream>
#include <iostream>

typedef float data_type;

#include "benchmark.hpp"

Stopwatch setup_timer, generate_timer, fft_timer, normalize_timer, copy_timer, write_timer;

#include "kernel.hpp"

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
    start_timer(setup_timer);
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
        ("inverse", "Transform filterbank to wave")
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
    // Read arguments and set up
    // ------------
    size_t in_nsamp_seg = vm["nsamp_seg"].as<size_t>();
    size_t seg_count = vm["seg_count"].as<size_t>();
    size_t out_nsamp_seg = 1 + in_nsamp_seg / 2;
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
    size_t seg_count_all = h_in.size() / in_nsamp_seg;
    size_t out_file_nsamps = out_nsamp_seg * seg_count_all;
    size_t out_part_file_nsamps = out_part_nsamp_seg * seg_count_all;
    std::vector<data_type> h_out_complex(2 * out_file_nsamps), h_out_real(out_file_nsamps);
    std::vector<data_type> h_out_part(out_part_file_nsamps);

    // Set up device side
    namespace bc = boost::compute;
    bc::command_queue queue = bc::system::default_queue();
    bc::context context = queue.get_context();
    bc::device device = queue.get_device();
    clfft_caller<data_type> fft_caller(queue, in_nsamp_seg, seg_count, out_nsamp_seg);
    // ------------
    stop_timer(setup_timer);
    std::cout << "setup_timer: " << setup_timer.getTime() << std::endl;

    // ------------
    // Print info
    // ------------
    /* clang-format off */
    std::cout << "nsamp_seg = " << in_nsamp_seg << std::endl
              << "seg_count = " << seg_count << std::endl
              << "in_file_name = " << in_file_name << std::endl
              << "in_file_nsamps = " << in_file_nsamps << std::endl
              << "fmin = " << fmin << "  " << "fmax = " << fmax << "  " << "df = " << df << std::endl
              << "fmin_id = " << fmin_id << "  " << "fmax_id = " << fmax_id << std::endl;
    /* clang-format on */

    std::cout << "Using device " << device.name() << " on platform " << device.platform().name() << std::endl;
    // ------------

    /*
    // ------------
    start_timer(generate_timer);
    fft_caller.generate();
    stop_timer(generate_timer);
    std::cout << "generate_timer: " << generate_timer.getTime() << std::endl;
    // ------------
*/

    // ------------
    // Do fft
    // ------------
    fft_caller.call_fft(h_in, h_out_complex, h_out_real);
    std::cout << "\nfft_timer (average): " << fft_timer.getAverageTime() << " ms" << std::endl;
    // ------------

    fft_caller.teardown();

    start_timer(copy_timer);
    for (size_t i = 0; i < seg_count_all; i++) {
        for (size_t j = 0; j < out_part_nsamp_seg; j++) {
            assert(out_part_nsamp_seg * i + j < h_out_part.size());
            assert(out_nsamp_seg * i + (fmax_id - j) < h_out_real.size());
            h_out_part[out_part_nsamp_seg * i + j] = h_out_real[out_nsamp_seg * i + (fmax_id - j)];
        }
        // write_vector(h_out_part, out_part_nsamp_seg, seg_count_all, std::string("fil-test-part") + std::to_string(i) + ".txt");
    }
    stop_timer(copy_timer);

    // ------------
    start_timer(write_timer);
    write_vector(h_in, in_file_nsamps, 1, "fil-test-in-1d.txt");
    write_vector(h_in, in_nsamp_seg, seg_count_all, "fil-test-in-2d.txt");
    write_vector(h_out_complex, 2 * out_nsamp_seg, seg_count_all, "fil-test-complex.txt");
    write_vector(h_out_real, out_nsamp_seg, seg_count_all, "fil-test-real.txt");
    write_vector(h_out_part, out_part_nsamp_seg, seg_count_all, "fil-test-part.txt");
    stop_timer(write_timer);
    std::cout << "write_timer: " << write_timer.getTime() << std::endl;
    // ------------

    return 0;
}
