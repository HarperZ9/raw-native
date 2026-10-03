#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace raw {
// SHA-256 (FIPS 180-4), standard library only. Returns 64 lowercase hex digits.
std::string sha256Hex(const void* data, std::size_t len);
std::string sha256Hex(const std::string& bytes);
// Hash a file's bytes. Returns an empty string when the file cannot be read.
std::string sha256File(const std::string& path);
}
