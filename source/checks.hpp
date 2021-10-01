/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/

#pragma once
#ifndef _CHECKS_CPP
#define _CHECKS_CPP

#include <cstddef>

template <typename M>
inline M divide_all(M m, M n) {
    M ret = m;
    while (ret % n == 0) {
        ret /= n;
    }
    return ret;
}

inline bool check_fft_length(size_t l) {
    return divide_all(divide_all(divide_all(divide_all(l, static_cast<size_t>(2)), static_cast<size_t>(3)), static_cast<size_t>(5)), static_cast<size_t>(7)) == 1;
}

#endif // _CHECKS_CPP