#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aes {

using Key128 = std::array<uint8_t, 16>;

std::string toHex(const Key128& key);
bool parseHexKey(const std::string& hex, Key128& out);
Key128 randomKey128();

std::vector<uint8_t> pkcs7Pad(const std::vector<uint8_t>& data);
bool pkcs7Unpad(std::vector<uint8_t>& data);

std::vector<uint8_t> cryptData(const std::vector<uint8_t>& input, const Key128& key, bool decrypt);

} // namespace aes
