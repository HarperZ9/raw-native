#pragma once
// Private to raw_native_cli art: options, a small JSON writer, code statistics and PNG output
// shared by every feature's report.
#include "raw/renderer/art_metrics.hpp"
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
namespace raw::art_cmd {

struct Options {
    std::string feature, out{"art-out"};
    int threads{8}, frames{48}, repeats{50};
    bool orbit{true}, gpu{false}, cost{false};
};

// Code differences between two RGBA code images over the colour channels.
struct CodeStats { int max{0}, p999{0}; double mean{0}; long differ{0}, over2{0}, n{0}; };
CodeStats codeStats(const std::vector<std::uint8_t>& want, const std::vector<std::uint8_t>& got);
std::vector<std::uint8_t> unpack(const std::vector<std::uint32_t>& words);
// Writes RGBA codes as an RGB PNG; false when the file cannot be written.
bool writeCodesPng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba);
std::string hex(std::uint64_t v);

// A JSON object written field by field.
class Json {
public:
    Json& num(const char* k, double v);
    Json& integer(const char* k, long long v);
    Json& boolean(const char* k, bool v);
    Json& str(const char* k, const std::string& v);
    Json& raw(const char* k, const std::string& json);
    Json& nums(const char* k, const std::vector<double>& v);
    std::string done() const { return "{" + s_.str() + "}"; }
private:
    std::ostringstream s_;
    bool first_{true};
    void key(const char* k);
};
std::string pairedJson(const art::Paired& p);
std::string statsJson(const CodeStats& s);
bool writeText(const std::string& path, const std::string& text);

int strokes(const Options& o);

}  // namespace raw::art_cmd
