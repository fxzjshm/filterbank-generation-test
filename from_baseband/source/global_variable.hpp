/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#pragma once
#ifndef _GLOBAL_VARIABLE_HPP
#define _GLOBAL_VARIABLE_HPP

#include "stopwatch.h"
#include <boost/program_options.hpp>

extern boost::program_options::variables_map vm;

extern Stopwatch setup_timer, generate_timer, fft_timer, normalize_timer, copy_timer, write_timer;

#endif // _GLOBAL_VARIABLE_HPP