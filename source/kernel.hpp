/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#include "benchmark.hpp"
#include <boost/compute.hpp>
#include <clFFT.h>

extern Stopwatch setup_timer, generate_timer, fft_timer, normalize_timer, copy_timer, write_timer;

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
        d_out_real[i] = sqrt(sqrt(d_out_complex[2 * i] * d_out_complex[2 * i] + d_out_complex[2 * i + 1] * d_out_complex[2 * i + 1]));
    });

template <typename data_type>
class clfft_caller {

public:
    boost::compute::command_queue queue;
    size_t in_nsamp_seg, out_nsamp_seg;
    size_t seg_count;
    size_t in_nsamp, out_nsamp;
    boost::compute::vector<data_type> d_in, d_out_complex, d_out_real;
    clfftPlanHandle plan_handle;
    boost::compute::kernel generate_kernel;
    boost::compute::kernel normalize_kernel;

    clfft_caller(boost::compute::command_queue queue_, size_t in_nsamp_seg_, size_t seg_count_, size_t out_nsamp_seg_)
        : queue(queue_), in_nsamp_seg(in_nsamp_seg_), seg_count(seg_count_), out_nsamp_seg(out_nsamp_seg_),
          in_nsamp(in_nsamp_seg * seg_count), out_nsamp(out_nsamp_seg * seg_count), d_in(in_nsamp), d_out_complex(2 * out_nsamp), d_out_real(out_nsamp) {

        namespace bc = boost::compute;
        bc::context context = queue.get_context();
        bc::device device = queue.get_device();
        clfftSetupData clfft_setup_data;
        clfftInitSetupData(&clfft_setup_data);
        clfftSetup(&clfft_setup_data);
        clfftDim dim = CLFFT_1D;
        size_t cl_lengths[1] = {in_nsamp_seg};
        clfftCreateDefaultPlan(&plan_handle, context.get(), dim, cl_lengths);
        clfftSetPlanPrecision(plan_handle, CLFFT_SINGLE);
        clfftSetLayout(plan_handle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
        clfftSetResultLocation(plan_handle, CLFFT_OUTOFPLACE);
        clfftSetPlanBatchSize(plan_handle, seg_count);
        clfftSetPlanDistance(plan_handle, in_nsamp_seg, out_nsamp_seg);
        clfftBakePlan(plan_handle, 1, &(queue.get()), NULL, NULL);

        bc::program program = bc::program::build_with_source(kernel_source, context, std::string("-Ddata_type=") + bc::type_name<data_type>());
        generate_kernel = bc::kernel(program, "generate");
        normalize_kernel = bc::kernel(program, "normalize_complex_number");
    }

    void print_info() {
        /* clang-format off */
        std::cout << "in_nsamp_seg = " << in_nsamp_seg << "  " << "out_nsamp_seg = " << out_nsamp_seg << "  " << "seg_count = " << seg_count << std::endl
                  << "in_nsamp = " << in_nsamp << "  " << "out_nsamp = " << out_nsamp << std::endl;
        /* clang-format on */
        std::cout << "d_in size : " << d_in.get_buffer().get_memory_size() << " bytes" << std::endl;
    }

    void generate() {
        generate_kernel.set_args(d_in.get_buffer().get(), static_cast<cl_ulong>(in_nsamp));
        queue.enqueue_1d_range_kernel(generate_kernel, 0, in_nsamp, 0);
    }

    void call_fft(std::vector<data_type> &h_in, std::vector<data_type> &h_out_complex, std::vector<data_type> &h_out_real) {
        namespace bc = boost::compute;
        size_t iteration = h_in.size() / in_nsamp;
        size_t in_offset = 0, out_offset = 0, out_part_offset = 0;
        for (size_t i = 0; i < iteration; i++) {
            start_timer(copy_timer);
            bc::copy(h_in.begin() + in_offset, h_in.begin() + in_offset + in_nsamp, d_in.begin());
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

            in_offset += in_nsamp;
            out_offset += out_nsamp;
            //out_part_offset += out_part_nsamp;
        }
    }

    void teardown() {
        clfftDestroyPlan(&plan_handle);
        clfftTeardown();
    }
};