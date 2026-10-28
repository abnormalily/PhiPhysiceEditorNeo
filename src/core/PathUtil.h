#pragma once
#include <filesystem>
#include <string>

namespace ppn {
// UTF-8 representation for display and for raylib-style APIs.
inline std::string pathUtf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
} // namespace ppn
