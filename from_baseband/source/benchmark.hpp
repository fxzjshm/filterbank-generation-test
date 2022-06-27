/***************************************************************************
 *
 *   Copyright (C) 2012 by Ben Barsdell and Andrew Jameson
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

// copied from OpenCL port of heimdall

#pragma once
#ifndef HD_BENCHMARK_HPP
#define HD_BENCHMARK_HPP

#include "stopwatch.h"
#include <boost/compute/system.hpp>

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

#endif // HD_BENCHMARK_HPP