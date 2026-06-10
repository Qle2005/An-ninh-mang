#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> readBinaryFile(const std::string& path);
std::vector<uint8_t> readBinaryFile(const std::wstring& path);
bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data);
bool writeBinaryFile(const std::wstring& path, const std::vector<uint8_t>& data);
