#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

bool sha256Bytes(const void *data, std::size_t size, std::string &hex, std::string &error);
bool sha256File(const std::filesystem::path &path, std::string &hex, std::string &error);
