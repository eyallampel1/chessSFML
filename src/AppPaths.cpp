#include "AppPaths.h"

#include <filesystem>

#include <windows.h>

namespace apppaths {
namespace {

std::string computeExecutableDir() {
    // The path can exceed MAX_PATH, so grow the buffer until it fits.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (len == 0) return ".";
        if (len < buffer.size()) {
            buffer.resize(len);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    std::error_code ec;
    const std::filesystem::path exe(buffer);
    const std::filesystem::path dir = exe.parent_path();
    if (dir.empty()) return ".";
    // Narrow through the native code page: SFML and the engine launcher both
    // take narrow paths.
    return dir.string();
}

bool exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

} // namespace

const std::string& executableDir() {
    static const std::string dir = computeExecutableDir();
    return dir;
}

std::string resource(const std::string& relative) {
    const std::string packaged = executableDir() + "\\" + relative;
    if (exists(packaged)) return packaged;

    if (exists(relative)) return relative;   // started from the project root

    // build/bin/runner.exe run from the IDE: the data is two levels up.
    const std::string fromBuildDir = executableDir() + "\\..\\..\\" + relative;
    if (exists(fromBuildDir)) return fromBuildDir;

    return packaged;
}

} // namespace apppaths
