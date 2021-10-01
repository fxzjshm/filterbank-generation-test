/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

// pad missing channels with 0
// used for converting filterbank file to wave

#include <boost/program_options.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>

#include "io.hpp"
#include "types.h"

// Edited from https://www.geeksforgeeks.org/program-find-gcd-floating-point-numbers/
template <typename T, typename U, typename V>
T gcd(T a, U b, V epsilon) {
    if (a < b) {
        std::swap(a, b);
    }

    if (std::abs(b) < epsilon) {
        return a;
    }
    return (gcd(b, a - std::floor(a / b) * b, epsilon));
}

int main(int argc, char **argv) {
    boost::program_options::options_description all_option("Options");
    using boost::program_options::value;
    /* clang-format off */
    all_option.add_options()
        ("help,h", "Show help message")
        ("samples_count", value<size_t>()->required(), "Number of samples in file")
        ("input_file,f", value<std::string>()->required(), "Input file")
        ("output_file,o", value<std::string>()->required(), "Output file")
        ("in_text", "Read input file as text")
        ("out_text", "Write output file as text")
        ("fmax", value<float>()->required(), "Max of frequency of input channel")
        ("df", value<float>()->required(), "Input Channel bandwidth")
        ("nchans", value<size_t>()->required(), "Number of channels in input file")
        ("epsilon", value<float>()->required(), "Minial channel bandwidth in output file")
        ("fmax_out", value<float>()->required(), "Max of frequency of out channel")
        ("pad_value_real,pad_value", value<float>()->default_value(0.0f), "Value to fill real part in padded area")
        ("pad_imaginary_part", "Whether to pad imaginary part")
        ("pad_value_imaginary", value<float>()->default_value(0.0f), "Value to fill real part in padded area")
    ;
    /* clang-format on */
    boost::program_options::positional_options_description p;
    p.add("input_file", 1);
    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(all_option).positional(p).run(), vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
        std::cout << all_option << std::endl;
        return 0;
    }

    float in_fmax = vm["fmax"].as<float>();
    float in_df = std::abs(vm["df"].as<float>());
    float gcd_epsilon = vm["epsilon"].as<float>();
    float out_df = gcd(in_fmax, in_df, gcd_epsilon);
    size_t nchans = vm["nchans"].as<size_t>();
    float in_fmin = in_fmax - in_df * (nchans - 1);
    size_t samples_count = vm["samples_count"].as<size_t>();
    float out_fmax;
    if (vm.count("fmax_out")) {
        out_fmax = vm["fmax_out"].as<float>();
    } else {
        out_fmax = in_fmax;
    }
    if (out_fmax < in_fmax) {
        std::cerr << "Warn: setting out_fmax to in_max" << std::endl;
    }
    size_t out_seg_length = static_cast<size_t>(std::round(out_fmax / out_df)) + 1;
    float pad_value_real = vm["pad_value_real"].as<float>();

    std::cout << "out_df = " << out_df << std::endl
              << "out_seg_length = " << out_seg_length << std::endl;

    std::vector<data_type> h_in;
    std::string in_file_name = vm["input_file"].as<std::string>();
    std::string out_file_name = vm["output_file"].as<std::string>();
    size_t in_file_nsamps;
    if (vm.count("in_text")) {
        std::ifstream in_file_stream(in_file_name);
        // h_in = std::vector<data_type>(std::istream_iterator<data_type>(in_file_stream), {});
        data_type tmp;
        while (in_file_stream >> tmp) {
            h_in.push_back(tmp);
        }
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
    write_vector(h_in, "pad_fil-h_in.txt");

    std::vector<data_type> h_out_real; // output is padded filterbank
    std::fill(h_out_real.begin(), h_out_real.end(), pad_value_real);
    h_out_real.resize(out_seg_length * samples_count, pad_value_real);

    for (size_t s = 0; s < samples_count; s++) {
        for (size_t i = 0; i < nchans; i++) {
            size_t in_idx = nchans * s + i;
            float f_current = in_fmax - in_df * i;
            size_t f_current_idx = static_cast<size_t>(std::round(f_current / out_df));
            assert(f_current_idx < out_seg_length);
            size_t out_idx = out_seg_length * s + f_current_idx;
            if (in_idx < h_in.size() && out_idx < h_out_real.size()) {
                h_out_real[out_idx] = h_in[in_idx];
            } else {
                std::cout << "Warning: "
                          << "in_idx = " << in_idx << ", "
                          << "h_in.size() = " << h_in.size() << ", "
                          << "out_idx = " << out_idx << ", "
                          << "h_out_real.size() = " << h_out_real.size() << std::endl;
            }
        }
    }

    if (vm.count("pad_imaginary_part")) {
        float pad_value_imaginary = vm["pad_value_imaginary"].as<float>();
        std::vector<data_type> h_out_complex(2 * h_out_real.size());
        for (size_t i = 0; i < h_out_real.size(); i++) {
            h_out_complex[2 * i] = h_out_real[i];
            h_out_complex[2 * i + 1] = pad_value_imaginary;
        }
        if (vm.count("out_text")) {
            write_vector(h_out_complex, 2 * out_seg_length, samples_count, out_file_name);
        } else {
            write_vector_binary(h_out_complex, 2 * out_seg_length * samples_count, out_file_name);
        }
    } else {
        if (vm.count("out_text")) {
            write_vector(h_out_real, out_seg_length, samples_count, out_file_name);
        } else {
            write_vector_binary(h_out_real, out_seg_length * samples_count, out_file_name);
        }
    }

    return 0;
}