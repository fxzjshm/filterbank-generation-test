/***************************************************************************
 *
 *   Copyright (C) 2021 by fxzjshm
 *   Licensed under the GNU General Public License, version 2.0
 *
 ***************************************************************************/
#pragma once
#ifndef _IO_HPP
#define _IO_HPP

#include <string>
#include <fstream>
#include <sstream>

template <typename Vec>
void write_vector(Vec &vec, size_t width, size_t height, std::string name) {
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

template <typename Vec>
void write_vector(Vec &vec, size_t length, std::string name) {
    return write_vector(vec, length, 1, name);
}

template <typename Vec>
void write_vector(Vec &vec, std::string name) {
    return write_vector(vec, vec.size(), name);
}

template <typename Vec>
void write_vector_binary(Vec &vec, size_t length, std::string name) {
    typedef typename Vec::value_type E;
    FILE* file_handle;
    file_handle = fopen(name.c_str(), "wb");
    fwrite(&vec[0], sizeof(E), length, file_handle);
    fclose(file_handle);
}

#endif // _IO_HPP