// Mock FsApiConstants.h for native testing
#pragma once

#include <cstdint>

using oflag_t = uint8_t;
constexpr oflag_t O_RDONLY = 0x00;
constexpr oflag_t O_WRONLY = 0x01;
constexpr oflag_t O_RDWR = 0x02;
constexpr oflag_t O_CREAT = 0x04;
constexpr oflag_t O_TRUNC = 0x08;
