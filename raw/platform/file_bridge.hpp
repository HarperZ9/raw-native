// The file bridge a script host gives a game: five synchronous calls over one root folder.
// Paths are relative with forward slashes; anything absolute, drive-lettered or containing ".."
// is refused. write() flushes to the disk before it returns; rename() replaces the target in
// one step (MoveFileExW with REPLACE_EXISTING and WRITE_THROUGH). Errors throw std::runtime_error.
// Windows only; the contract is the one Low Water's save/filestore.mjs states.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace raw::platform {
class FileBridge {
public:
    explicit FileBridge(std::filesystem::path root);
    static bool path_ok(const std::string& rel);
    std::optional<std::string> read(const std::string& rel) const;   // nullopt when the file does not exist
    void write(const std::string& rel, const std::string& bytes) const;   // creates folders; flushed
    void rename(const std::string& from, const std::string& to) const;
    void remove(const std::string& rel) const;                       // no error when missing
    std::vector<std::string> list(const std::string& dir) const;     // names in dir, empty when missing
    const std::filesystem::path& root() const { return root_; }
private:
    std::filesystem::path abs(const std::string& rel) const;
    std::filesystem::path root_;
};
}  // namespace raw::platform
