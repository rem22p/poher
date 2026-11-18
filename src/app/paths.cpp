#include "app/paths.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace poher::app {

std::filesystem::path program_folder(const char* argv0) {
    std::error_code error;
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) return std::filesystem::path(buffer.data()).parent_path();
#elif defined(__APPLE__)
    std::vector<char> buffer(4096);
    auto size = static_cast<uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        _NSGetExecutablePath(buffer.data(), &size);
    }
    const std::filesystem::path real = std::filesystem::canonical(buffer.data(), error);
    if (!error) return real.parent_path();
#else
    const std::filesystem::path real = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return real.parent_path();
#endif
    const std::filesystem::path given = std::filesystem::absolute(argv0, error);
    return error ? std::filesystem::current_path(error) : given.parent_path();
}

std::filesystem::path default_avatar_folder(const char* argv0) {
    const std::filesystem::path here = program_folder(argv0);
    std::error_code error;
    for (const std::filesystem::path& folder : {here / "avatars", here.parent_path() / "avatars"}) {
        if (std::filesystem::is_directory(folder, error)) return folder;
    }
    return here / "avatars";
}

std::filesystem::path default_data_folder(const char* argv0) {
#if defined(_WIN32)
    if (const wchar_t* roaming = _wgetenv(L"APPDATA"); roaming != nullptr && *roaming != 0) {
        return std::filesystem::path(roaming) / "POHER";
    }
#else
    const char* home = std::getenv("HOME");
#if defined(__APPLE__)
    if (home != nullptr && *home != 0) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "POHER";
    }
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != 0) {
        return std::filesystem::path(data) / "poher";
    }
    if (home != nullptr && *home != 0) return std::filesystem::path(home) / ".local" / "share" / "poher";
#endif
#endif
    return program_folder(argv0) / "data";
}

}  // namespace poher::app
