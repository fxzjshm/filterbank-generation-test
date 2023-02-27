#if 0
    EXEC=${0%.*}
    c++ "$0" -std=c++20 -o "$EXEC" -O3 -march=native -l boost_system
    exec "$EXEC"
#endif
// ^ ref: https://stackoverflow.com/questions/2482348/run-c-or-c-file-as-a-script

/******************************************************************************* 
 * Copyright (c) 2022 fxzjshm
 * This software is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 ******************************************************************************/

// @deprecated currently in favour of the rust version

// some notice:
// * avoid usage of pointers, use RAII instead
// * check index, do not write out of buffer

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/python.hpp>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace srtb {
namespace prototype {
namespace udp_receiver {

/** 
 * @brief just another implementations of part of sigproc headers 
 * ref: sigproc/filterbank_header.c
 */
namespace sigproc {
namespace filterbank_header {

template <typename Stream, typename T>
inline void send(Stream& stream, const T& value)
  requires(std::is_pod_v<T>)
{
  constexpr size_t size = sizeof(value);
  constexpr int32_t prepend_size = size;
  stream.write(reinterpret_cast<const char*>(&prepend_size),
               sizeof(prepend_size));
  stream.write(reinterpret_cast<const char*>(&value), size);
}

template <typename Stream, typename T>
inline void send(Stream& stream, const T& value)
  requires(std::is_same_v<T, std::string>)
{
  constexpr size_t size = value.size();
  constexpr int32_t prepend_size = size;
  stream.write(reinterpret_cast<const char*>(&prepend_size),
               sizeof(prepend_size));
  stream.write(value.c_str(), size);
}

template <typename Stream, typename T, typename... Args>
inline void send(Stream& stream, const T& value, Args... args) {
  send(stream, value);
  send(stream, args);
}

}  // namespace filterbank_header
}  // namespace sigproc

/** @brief global variables */
namespace global {
inline const std::string config_file_name = "srtb_config.py";

inline boost::program_options::variables_map config;
}  // namespace global

template <typename Stream>
inline void generate_filterbank_header(Stream& file_stream) {
  using namespace sigproc::filterbank_header;
  send(file_stream, "HEADER_START");
  send(file_stream, "telescope_id", srtb_config.telescope_id);
  send(file_stream, "machine_id", srtb_config.machine_id);
  send(file_stream, "rawdatafile", srtb_config.rawdatafile);
  send(file_stream, "source_name", srtb_config.source_name);
  send(file_stream, "data_type", srtb_config.data_type);
  send(file_stream, "fch1", srtb_config.fch1);
  send(file_stream, "foff", srtb_config.foff);
  send(file_stream, "nchans", srtb_config.nchans);
  send(file_stream, "tsamp", srtb_config.tsamp);
  send(file_stream, "nbeams", srtb_config.nbeams);
  send(file_stream, "nbits", srtb_config.nbits);
  send(file_stream, "nifs", srtb_config.nifs);
  send(file_stream, "src_raj", srtb_config.src_raj);
  send(file_stream, "src_dej", srtb_config.src_dej);
  send(file_stream, "tstart", srtb_config.tstart);
  send(file_stream, "nsamples", srtb_config.nsamples);
  send(file_stream, "HEADER_END");
}

inline void read_config() {
  boost::program_options::options_description dummy_option("");
  boost::program_options::store(
      boost::program_options::parse_config_file<char>(
          global::config_file_name.c_str(), dummy_option,
          /* allow_unregistered = */ true),
      global::config);
  boost::program_options::notify(global::config);
}

inline int main() { return EXIT_SUCCESS; }

}  // namespace udp_receiver
}  // namespace prototype
}  // namespace srtb

int main() { return srtb::prototype::udp_receiver::main(); }
