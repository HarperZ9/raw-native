// See raw/platform/file_bridge.hpp.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include "raw/platform/file_bridge.hpp"
namespace raw::platform {
namespace {
[[noreturn]] void fail(const char* what, const std::string& p) {
    throw std::runtime_error(std::string("bridge: ") + what + " " + p + " (win32 error " + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE h; explicit Handle(HANDLE x) : h(x) {} ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
}  // namespace
FileBridge::FileBridge(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_);
}
bool FileBridge::path_ok(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel[0] == char(92) || rel.find(char(92)) != std::string::npos) return false;
    if (rel.size() > 1 && rel[1] == ':') return false;
    size_t i = 0;
    while (i <= rel.size()) {
        size_t j = rel.find('/', i); if (j == std::string::npos) j = rel.size();
        if (rel.compare(i, j - i, "..") == 0) return false;
        i = j + 1;
    }
    return true;
}
std::filesystem::path FileBridge::abs(const std::string& rel) const {
    if (!path_ok(rel)) throw std::runtime_error("bridge: refused path " + rel);
    return root_ / std::filesystem::u8path(rel);
}
std::optional<std::string> FileBridge::read(const std::string& rel) const {
    const auto p = abs(rel);
    Handle f(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (f.h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return std::nullopt;
        fail("cannot open", rel);
    }
    LARGE_INTEGER sz{}; if (!GetFileSizeEx(f.h, &sz)) fail("cannot size", rel);
    std::string out(static_cast<size_t>(sz.QuadPart), '\0'); size_t got = 0;
    while (got < out.size()) {
        DWORD n = 0; if (!ReadFile(f.h, out.data() + got, static_cast<DWORD>(out.size() - got), &n, nullptr) || n == 0) break;
        got += n;
    }
    out.resize(got); return out;
}
void FileBridge::write(const std::string& rel, const std::string& bytes) const {
    const auto p = abs(rel);
    std::filesystem::create_directories(p.parent_path());
    Handle f(CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (f.h == INVALID_HANDLE_VALUE) fail("cannot create", rel);
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD n = 0;
        if (!WriteFile(f.h, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &n, nullptr)) fail("cannot write", rel);
        done += n;
    }
    if (!FlushFileBuffers(f.h)) fail("cannot flush", rel);
}
void FileBridge::rename(const std::string& from, const std::string& to) const {
    const auto a = abs(from), b = abs(to);
    std::filesystem::create_directories(b.parent_path());
    if (!MoveFileExW(a.c_str(), b.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail("cannot rename", from);
}
void FileBridge::remove(const std::string& rel) const {
    const auto p = abs(rel);
    if (!DeleteFileW(p.c_str())) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) fail("cannot remove", rel);
    }
}
std::vector<std::string> FileBridge::list(const std::string& dir) const {
    const auto p = abs(dir);
    std::vector<std::string> out; std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(p, ec)) { const auto u = e.path().filename().u8string(); out.emplace_back(reinterpret_cast<const char*>(u.data()), u.size()); }
    return out;
}
}  // namespace raw::platform
