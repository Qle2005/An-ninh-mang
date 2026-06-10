#include "file_utils.h"

#include <windows.h>

#include <algorithm>

namespace {

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
    return out;
}

} // namespace

std::vector<uint8_t> readBinaryFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(file);
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(fileSize.QuadPart));
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        DWORD bytesRead = 0;
        if (!ReadFile(file, data.data() + offset, chunk, &bytesRead, nullptr) || bytesRead == 0) {
            CloseHandle(file);
            return {};
        }
        offset += bytesRead;
    }

    CloseHandle(file);
    return data;
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    return readBinaryFile(utf8ToWide(path));
}

bool writeBinaryFile(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    size_t offset = 0;
    while (offset < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        DWORD bytesWritten = 0;
        if (!WriteFile(file, data.data() + offset, chunk, &bytesWritten, nullptr) ||
            bytesWritten != chunk) {
            CloseHandle(file);
            return false;
        }
        offset += bytesWritten;
    }

    CloseHandle(file);
    return true;
}

bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
    return writeBinaryFile(utf8ToWide(path), data);
}
