// The file bridge: round trip, atomic replace, refused paths, missing files, list.
#include "check.hpp"
#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include "raw/platform/file_bridge.hpp"
using raw::platform::FileBridge;
int main() {
    const auto dir = std::filesystem::temp_directory_path() / ("raw_fb_test_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(dir);
    {
        FileBridge b(dir.string());
        CHECK(!b.read("saves/slot-auto.json").has_value());
        CHECK(b.list("saves").empty());
        b.write("saves/slot-auto.json.tmp", "{\"a\":1}");
        CHECK(b.read("saves/slot-auto.json.tmp").value() == "{\"a\":1}");
        b.write("saves/slot-auto.json", "old");
        b.rename("saves/slot-auto.json.tmp", "saves/slot-auto.json");   // replaces an existing file
        CHECK(b.read("saves/slot-auto.json").value() == "{\"a\":1}");
        CHECK(!b.read("saves/slot-auto.json.tmp").has_value());
        const std::string utf8 = "caf\xC3\xA9 \xE2\x80\x94 \n\r\n";
        b.write("settings/p.json", utf8);
        CHECK(b.read("settings/p.json").value() == utf8);             // bytes are kept exactly
        CHECK(b.list("saves").size() == 1);
        b.remove("saves/slot-auto.json"); b.remove("saves/slot-auto.json");   // second is no error
        CHECK(b.list("saves").empty());
        for (const char* bad : {"../x", "a/../x", "/abs", "C:/x", "a\b", ""}) {
            bool threw = false; try { b.write(bad, "x"); } catch (const std::exception&) { threw = true; }
            CHECK(threw);
        }
        CHECK(!FileBridge::path_ok("..") && FileBridge::path_ok("a/b.json") && FileBridge::path_ok("a..b/c"));
        std::string big(3 * 1024 * 1024, 'x'); b.write("world/big.json", big);
        CHECK(b.read("world/big.json").value() == big);
    }
    std::filesystem::remove_all(dir);
    return raw_test_summary();
}
#else
int main() { return 0; }   // the bridge is Windows only
#endif
