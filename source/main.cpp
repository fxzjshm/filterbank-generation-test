/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#define BOOST_COMPUTE_DEBUG_KERNEL_COMPILATION

#include "stopwatch.h"
#include <boost/compute.hpp>
#include <clFFT.h>
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
    __kernel void generate(__global data_type *d_in, ulong nsamps /*, data_type dt*/) {
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
        d_out_real[i] = sqrt(d_out_complex[2 * i] * d_out_complex[2 * i] + d_out_complex[2 * i + 1] * d_out_complex[2 * i + 1]);
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
    if (argc < 3) {
        std::cout << argv[0] << " <nsamp_gulp> <seg_count> [iteration]" << std::endl;
        exit(-1);
    }
    size_t nsamp_gulp = std::atoi(argv[1]);
    size_t seg_count = std::atoi(argv[2]);
    size_t iteration = 1;
    if (argc >= 4) {
        iteration = std::atoi(argv[3]);
    }
    size_t nsamps = nsamp_gulp * seg_count;
    std::cout << "nsamp_gulp = " << nsamp_gulp << std::endl
              << "seg_count = " << seg_count << std::endl
              << "nsamps = " << nsamps << std::endl
              << "iteration = " << iteration << std::endl;
    // data_type dt = 0.2;

    namespace bc = boost::compute;
    bc::command_queue queue = bc::system::default_queue();
    bc::context context = queue.get_context();
    bc::device device = queue.get_device();
    std::cout << "Using device " << device.name() << " on platform " << device.platform().name() << std::endl;

    Stopwatch setup_timer, generate_timer, fft_timer, normalize_timer, copy_timer, write_timer;

    // ------------
    start_timer(setup_timer);
    clfftSetupData clfft_setup_data;
    clfftInitSetupData(&clfft_setup_data);
    clfftSetup(&clfft_setup_data);
    clfftPlanHandle plan_handle;
    clfftDim dim = CLFFT_1D;
    size_t cl_lengths[1] = {nsamp_gulp};
    clfftCreateDefaultPlan(&plan_handle, context.get(), dim, cl_lengths);
    clfftSetPlanPrecision(plan_handle, CLFFT_SINGLE);
    clfftSetLayout(plan_handle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
    clfftSetResultLocation(plan_handle, CLFFT_OUTOFPLACE);
    clfftSetPlanBatchSize(plan_handle, seg_count);
    clfftSetPlanDistance(plan_handle, nsamp_gulp, nsamp_gulp / 2);
    clfftBakePlan(plan_handle, 1, &(queue.get()), NULL, NULL);

    bc::vector<data_type> d_in(nsamps), d_out_complex(nsamps);
    bc::program program = bc::program::build_with_source(kernel_source, context, std::string("-Ddata_type=") + bc::type_name<data_type>());
    bc::kernel generate_kernel(program, "generate");
    bc::kernel normalize_kernel(program, "normalize_complex_number");
    stop_timer(setup_timer);
    std::cout << "setup_timer: " << setup_timer.getTime() << std::endl;
    std::cout << "d_in size : " << d_in.get_buffer().get_memory_size() << " bytes" << std::endl;
    // ------------

    // ------------
    start_timer(generate_timer);
    generate_kernel.set_args(d_in.get_buffer().get(), static_cast<cl_ulong>(nsamps) /*, dt */);
    queue.enqueue_1d_range_kernel(generate_kernel, 0, nsamps, 0);
    stop_timer(generate_timer);
    std::cout << "generate_timer: " << generate_timer.getTime() << std::endl;
    // ------------

    // ------------
    for (size_t i = 0; i < iteration; i++) {
        start_timer(fft_timer);
        clfftEnqueueTransform(plan_handle, CLFFT_FORWARD, 1, &(queue.get()), 0, NULL, NULL, &(d_in.get_buffer().get()), &(d_out_complex.get_buffer().get()), NULL);
        stop_timer(fft_timer);
        std::cout << "fft_timer: " << fft_timer.getTime() << "    \r";
        std::cout.flush();
    }
    std::cout << "\nfft_timer (average): " << fft_timer.getAverageTime() << " ms" << std::endl;
    // ------------

    d_in.clear();
    d_in.shrink_to_fit();
    bc::vector<data_type> d_out_real(nsamps / 2);

    // ------------
    start_timer(normalize_timer);
    normalize_kernel.set_args(d_out_complex.get_buffer().get(), d_out_real.get_buffer().get());
    queue.enqueue_1d_range_kernel(normalize_kernel, 0, nsamps / 2, 0);
    queue.finish();
    stop_timer(normalize_timer);
    std::cout << "normalize_timer: " << normalize_timer.getTime() << std::endl;
    // ------------

    // ------------
    start_timer(copy_timer);
    std::vector<data_type> h_in(nsamps), h_out_complex(nsamps), h_out_real(nsamps / 2);
    bc::copy(d_in.begin(), d_in.end(), h_in.begin());
    bc::copy(d_out_complex.begin(), d_out_complex.end(), h_out_complex.begin());
    bc::copy(d_out_real.begin(), d_out_real.end(), h_out_real.begin());
    queue.finish();
    stop_timer(copy_timer);
    std::cout << "copy_timer: " << copy_timer.getTime() << std::endl;
    // ------------

    clfftDestroyPlan(&plan_handle);
    clfftTeardown();

    // ------------
    start_timer(write_timer);
    write_vector(h_in, nsamps, 1, "fil-test-in-1d.txt");
    write_vector(h_in, nsamp_gulp, seg_count, "fil-test-in-2d.txt");
    write_vector(h_out_complex, nsamp_gulp, seg_count, "fil-test-complex.txt");
    write_vector(h_out_real, nsamp_gulp / 2, seg_count, "fil-test-real.txt");
    stop_timer(write_timer);
    std::cout << "write_timer: " << write_timer.getTime() << std::endl;
    // ------------

    return 0;
}
