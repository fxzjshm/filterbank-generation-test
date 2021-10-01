/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#include "benchmark.hpp"
#include "checks.hpp"
#include "io.hpp"
#include <boost/compute/command_queue.hpp>
#include <boost/compute/container/vector.hpp>
#include <boost/compute/utility/source.hpp>
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
        d_out_real[i] = (sqrt(d_out_complex[2 * i] * d_out_complex[2 * i] + d_out_complex[2 * i + 1] * d_out_complex[2 * i + 1]));
    }

    __kernel void pick_real_in_complex_number(__global data_type *d_out_complex, __global data_type *d_out_real) {
        size_t i = get_global_id(0);
        d_out_real[i] = d_out_complex[2 * i];
    });

template <typename data_type>
class clfft_caller {

public:
    boost::compute::command_queue queue;
    size_t in_nsamp_seg, out_nsamp_seg;
    size_t seg_count;
    size_t in_nsamp, out_nsamp;
    boost::compute::vector<data_type> d_in, d_out_complex, d_out_real, d_in_tmp;
    clfftPlanHandle plan_handle;
    boost::compute::kernel generate_kernel;
    boost::compute::kernel normalize_kernel;
    boost::compute::kernel pick_real_kernel;
    bool inverse;

    clfft_caller(boost::compute::command_queue queue_, size_t in_nsamp_seg_, size_t seg_count_, size_t out_nsamp_seg_, bool inverse_)
        : queue(queue_), in_nsamp_seg(in_nsamp_seg_), seg_count(seg_count_), out_nsamp_seg(out_nsamp_seg_), inverse(inverse_),
          in_nsamp(in_nsamp_seg * seg_count), out_nsamp(out_nsamp_seg * seg_count), d_in(in_nsamp), d_out_complex(2 * out_nsamp), d_out_real(out_nsamp) {

        namespace bc = boost::compute;
        bc::context context = queue.get_context();
        bc::device device = queue.get_device();
        clfftSetupData clfft_setup_data;
        clfftInitSetupData(&clfft_setup_data);
        clfftSetup(&clfft_setup_data);
        clfftDim dim = CLFFT_1D;
        size_t cl_lengths[1];
        if (!inverse) {
            cl_lengths[0] = in_nsamp_seg;
        } else {
            cl_lengths[0] = out_nsamp_seg;
        }
        if (!check_fft_length(cl_lengths[0])) {
            throw std::runtime_error("Unsupported fft length " + std::to_string(cl_lengths[0]));
        }
        clfftCreateDefaultPlan(&plan_handle, context.get(), dim, cl_lengths);
        clfftSetPlanPrecision(plan_handle, CLFFT_SINGLE);
        if (!inverse) {
            clfftSetLayout(plan_handle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
        } else {
            clfftSetLayout(plan_handle, CLFFT_HERMITIAN_PLANAR, CLFFT_REAL);
            d_in_tmp = bc::vector<data_type>(in_nsamp);
            bc::fill(d_in_tmp.begin(), d_in_tmp.end(), static_cast<data_type>(0));
        }
        clfftSetResultLocation(plan_handle, CLFFT_OUTOFPLACE);
        clfftSetPlanBatchSize(plan_handle, seg_count);
        clfftSetPlanDistance(plan_handle, in_nsamp_seg, out_nsamp_seg);
        clfftBakePlan(plan_handle, 1, &(queue.get()), NULL, NULL);

        bc::program program = bc::program::build_with_source(kernel_source, context, std::string("-Ddata_type=") + bc::type_name<data_type>());
        generate_kernel = bc::kernel(program, "generate");
        normalize_kernel = bc::kernel(program, "normalize_complex_number");
        pick_real_kernel = bc::kernel(program, "pick_real_in_complex_number");
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

        std::vector<data_type> h_in_tmp(in_nsamp), h_out_complex_tmp(2 * out_nsamp), h_out_real_tmp(out_nsamp);
        for (size_t i = 0; i < iteration; i++) {
            start_timer(copy_timer);
            bc::copy(h_in.begin() + in_offset, h_in.begin() + in_offset + in_nsamp, d_in.begin());
            bc::copy(h_in.begin() + in_offset, h_in.begin() + in_offset + in_nsamp, h_in_tmp.begin());
            stop_timer(copy_timer);

            start_timer(fft_timer);
            if (!inverse) {
                clfftEnqueueTransform(plan_handle, CLFFT_FORWARD, 1, &(queue.get()), 0, NULL, NULL, &(d_in.get_buffer().get()), &(d_out_complex.get_buffer().get()), NULL);
            } else {
                cl_mem d_ins[2] = {(d_in.get_buffer().get()), d_in_tmp.get_buffer().get()};
                clfftEnqueueTransform(plan_handle, CLFFT_BACKWARD, 1, &(queue.get()), 0, NULL, NULL, &d_ins[0], &(d_out_real.get_buffer().get()), NULL);
            }
            stop_timer(fft_timer);

            start_timer(normalize_timer);
            if (!inverse) {
                normalize_kernel.set_args(d_out_complex.get_buffer().get(), d_out_real.get_buffer().get());
                queue.enqueue_1d_range_kernel(normalize_kernel, 0, out_nsamp, 0);
            }
            stop_timer(normalize_timer);

            start_timer(copy_timer);
            // bc::copy(d_in.begin(), d_in.end(), h_in.begin() + in_offset);
            bc::copy(d_out_complex.begin(), d_out_complex.end(), h_out_complex.begin() + 2 * out_offset);
            bc::copy(d_out_real.begin(), d_out_real.end(), h_out_real.begin() + out_offset);
            if (i == 0) {
                bc::copy(h_out_complex.begin() + 2 * out_offset, h_out_complex.begin() + 2 * out_offset + 2 * out_nsamp, h_out_complex_tmp.begin());
                bc::copy(h_out_real.begin() + out_offset, h_out_real.begin() + out_offset + out_nsamp, h_out_real_tmp.begin());
            }
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
            if (i == 0) {
                start_timer(write_timer);
                write_vector(h_in_tmp, in_nsamp_seg, seg_count, "h_in_tmp-" + std::to_string(i) + ".txt");
                write_vector(h_out_complex_tmp, out_nsamp_seg * 2, seg_count, "h_out_complex_tmp-" + std::to_string(i) + ".txt");
                write_vector(h_out_real_tmp, out_nsamp_seg, seg_count, "h_out_real_tmp-" + std::to_string(i) + ".txt");
                stop_timer(write_timer);
            }

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