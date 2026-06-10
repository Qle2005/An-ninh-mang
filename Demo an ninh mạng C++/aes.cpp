#include "aes.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace aes {

namespace {

constexpr size_t BlockSize = 16;
constexpr int Nb = 4;
constexpr int Nk = 4;
constexpr int Nr = 10;

uint8_t gfMul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        bool highBit = (a & 0x80) != 0;
        a <<= 1;
        if (highBit) a ^= 0x1B;
        b >>= 1;
    }
    return result;
}

uint8_t gfPow(uint8_t value, int power) {
    uint8_t result = 1;
    while (power > 0) {
        if (power & 1) result = gfMul(result, value);
        value = gfMul(value, value);
        power >>= 1;
    }
    return result;
}

uint8_t rotl8(uint8_t value, int shift) {
    return static_cast<uint8_t>((value << shift) | (value >> (8 - shift)));
}

uint8_t makeSBoxValue(uint8_t value) {
    uint8_t inverse = value == 0 ? 0 : gfPow(value, 254);
    return static_cast<uint8_t>(inverse ^ rotl8(inverse, 1) ^ rotl8(inverse, 2) ^
                                rotl8(inverse, 3) ^ rotl8(inverse, 4) ^ 0x63);
}

struct Tables {
    std::array<uint8_t, 256> sbox{};
    std::array<uint8_t, 256> invSbox{};

    Tables() {
        for (int i = 0; i < 256; ++i) {
            sbox[static_cast<size_t>(i)] = makeSBoxValue(static_cast<uint8_t>(i));
            invSbox[sbox[static_cast<size_t>(i)]] = static_cast<uint8_t>(i);
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

void addRoundKey(uint8_t state[4][4], const std::array<uint8_t, 176>& roundKeys, int round) {
    size_t offset = static_cast<size_t>(round) * BlockSize;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            state[row][col] ^= roundKeys[offset + col * 4 + row];
        }
    }
}

void subBytes(uint8_t state[4][4]) {
    const auto& sbox = tables().sbox;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            state[row][col] = sbox[state[row][col]];
        }
    }
}

void invSubBytes(uint8_t state[4][4]) {
    const auto& invSbox = tables().invSbox;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            state[row][col] = invSbox[state[row][col]];
        }
    }
}

void shiftRows(uint8_t state[4][4]) {
    for (int row = 1; row < 4; ++row) {
        uint8_t tmp[4];
        for (int col = 0; col < 4; ++col) tmp[col] = state[row][(col + row) % 4];
        for (int col = 0; col < 4; ++col) state[row][col] = tmp[col];
    }
}

void invShiftRows(uint8_t state[4][4]) {
    for (int row = 1; row < 4; ++row) {
        uint8_t tmp[4];
        for (int col = 0; col < 4; ++col) tmp[(col + row) % 4] = state[row][col];
        for (int col = 0; col < 4; ++col) state[row][col] = tmp[col];
    }
}

void mixColumns(uint8_t state[4][4]) {
    for (int col = 0; col < 4; ++col) {
        uint8_t s0 = state[0][col];
        uint8_t s1 = state[1][col];
        uint8_t s2 = state[2][col];
        uint8_t s3 = state[3][col];
        state[0][col] = static_cast<uint8_t>(gfMul(2, s0) ^ gfMul(3, s1) ^ s2 ^ s3);
        state[1][col] = static_cast<uint8_t>(s0 ^ gfMul(2, s1) ^ gfMul(3, s2) ^ s3);
        state[2][col] = static_cast<uint8_t>(s0 ^ s1 ^ gfMul(2, s2) ^ gfMul(3, s3));
        state[3][col] = static_cast<uint8_t>(gfMul(3, s0) ^ s1 ^ s2 ^ gfMul(2, s3));
    }
}

void invMixColumns(uint8_t state[4][4]) {
    for (int col = 0; col < 4; ++col) {
        uint8_t s0 = state[0][col];
        uint8_t s1 = state[1][col];
        uint8_t s2 = state[2][col];
        uint8_t s3 = state[3][col];
        state[0][col] = static_cast<uint8_t>(gfMul(14, s0) ^ gfMul(11, s1) ^ gfMul(13, s2) ^ gfMul(9, s3));
        state[1][col] = static_cast<uint8_t>(gfMul(9, s0) ^ gfMul(14, s1) ^ gfMul(11, s2) ^ gfMul(13, s3));
        state[2][col] = static_cast<uint8_t>(gfMul(13, s0) ^ gfMul(9, s1) ^ gfMul(14, s2) ^ gfMul(11, s3));
        state[3][col] = static_cast<uint8_t>(gfMul(11, s0) ^ gfMul(13, s1) ^ gfMul(9, s2) ^ gfMul(14, s3));
    }
}

std::array<uint8_t, 176> expandKey(const Key128& key) {
    std::array<uint8_t, 176> expanded{};
    std::copy(key.begin(), key.end(), expanded.begin());

    uint8_t rcon = 1;
    size_t bytesGenerated = BlockSize;
    uint8_t temp[4]{};
    const auto& sbox = tables().sbox;

    while (bytesGenerated < expanded.size()) {
        for (int i = 0; i < 4; ++i) temp[i] = expanded[bytesGenerated - 4 + i];

        if (bytesGenerated % (Nk * 4) == 0) {
            uint8_t first = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon;
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[first];
            rcon = gfMul(rcon, 2);
        }

        for (int i = 0; i < 4; ++i) {
            expanded[bytesGenerated] = expanded[bytesGenerated - Nk * 4] ^ temp[i];
            ++bytesGenerated;
        }
    }

    return expanded;
}

std::array<uint8_t, BlockSize> processBlock(const uint8_t* input, const std::array<uint8_t, 176>& roundKeys, bool decrypt) {
    uint8_t state[4][4]{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            state[row][col] = input[col * 4 + row];
        }
    }

    if (!decrypt) {
        addRoundKey(state, roundKeys, 0);
        for (int round = 1; round < Nr; ++round) {
            subBytes(state);
            shiftRows(state);
            mixColumns(state);
            addRoundKey(state, roundKeys, round);
        }
        subBytes(state);
        shiftRows(state);
        addRoundKey(state, roundKeys, Nr);
    } else {
        addRoundKey(state, roundKeys, Nr);
        for (int round = Nr - 1; round > 0; --round) {
            invShiftRows(state);
            invSubBytes(state);
            addRoundKey(state, roundKeys, round);
            invMixColumns(state);
        }
        invShiftRows(state);
        invSubBytes(state);
        addRoundKey(state, roundKeys, 0);
    }

    std::array<uint8_t, BlockSize> output{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            output[col * 4 + row] = state[row][col];
        }
    }
    return output;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string toHex(const Key128& key) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (uint8_t byte : key) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

bool parseHexKey(const std::string& hex, Key128& out) {
    if (hex.size() != 32) return false;
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    for (size_t i = 0; i < out.size(); ++i) {
        int high = hexValue(hex[i * 2]);
        int low = hexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

Key128 randomKey128() {
    Key128 key{};
    std::random_device rd;
    for (uint8_t& byte : key) {
        byte = static_cast<uint8_t>(rd());
    }
    return key;
}

std::vector<uint8_t> pkcs7Pad(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> padded = data;
    uint8_t padLen = static_cast<uint8_t>(BlockSize - (padded.size() % BlockSize));
    if (padLen == 0) padLen = static_cast<uint8_t>(BlockSize);
    padded.insert(padded.end(), padLen, padLen);
    return padded;
}

bool pkcs7Unpad(std::vector<uint8_t>& data) {
    if (data.empty() || data.size() % BlockSize != 0) return false;
    uint8_t padLen = data.back();
    if (padLen == 0 || padLen > BlockSize || padLen > data.size()) return false;
    for (size_t i = data.size() - padLen; i < data.size(); ++i) {
        if (data[i] != padLen) return false;
    }
    data.resize(data.size() - padLen);
    return true;
}

std::vector<uint8_t> cryptData(const std::vector<uint8_t>& input, const Key128& key, bool decrypt) {
    std::array<uint8_t, 176> roundKeys = expandKey(key);
    std::vector<uint8_t> output(input.size());

    for (size_t i = 0; i < input.size(); i += BlockSize) {
        auto block = processBlock(input.data() + i, roundKeys, decrypt);
        std::copy(block.begin(), block.end(), output.begin() + static_cast<std::ptrdiff_t>(i));
    }

    return output;
}

} // namespace aes
