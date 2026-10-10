#pragma once
// Private to src/renderer/gpu: a string made safe inside a JSON string literal (quotes,
// backslashes and control characters escaped), for the RT checks' error and adapter text.
#include <cstdio>
#include <string>
namespace raw::gpu_check {
inline std::string jsonText(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o;
}
}  // namespace raw::gpu_check
