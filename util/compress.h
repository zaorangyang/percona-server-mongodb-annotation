// @file compress.h

#pragma once

#include <string>

namespace mongo { 

    size_t compress(const char* input, size_t input_length, std::string* output);

    bool uncompress(const char* compressed, size_t compressed_length, std::string* uncompressed);

}


