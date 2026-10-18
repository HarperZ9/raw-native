// raw_native_cli style-parity: see raw/tools/style_cmd.hpp.
#include "raw/tools/style_cmd.hpp"
#include "raw/renderer/style_parity.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
namespace raw {
int styleParityCommand(int argc, char** argv) {
    std::string out, dir = "style-fixtures", only;
    bool cpuOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--fixtures") && i + 1 < argc) dir = argv[++i];
        else if (!std::strcmp(argv[i], "--cpu-only")) cpuOnly = true;
        else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else { std::printf("usage: raw_native_cli style-parity [--fixtures DIR] [--cpu-only] [--only GROUP] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = cpuOnly ? nullptr : rhi::device(why);
    const gpu_check::StyleParity r = gpu_check::styleParity(dev, dir, only);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    if (!r.error.empty() && r.cases.empty()) return 2;
    if (!cpuOnly && !dev) { std::fprintf(stderr, "style-parity: GPU link skipped: %s\n", why.c_str()); return r.cpuPass() ? 4 : 1; }
    return r.pass() ? 0 : 1;
}
int styleDispatchCommand(int argc, char** argv) {
    std::string out;
    int w = 3840, h = 2160;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) h = std::atoi(argv[++i]);
        else { std::printf("usage: raw_native_cli style-4k [--width W] [--height H] [--out FILE]\n"); return 2; }
    }
    if (w <= 0 || h <= 0) { std::printf("style-4k: width and height must be positive\n"); return 2; }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::fprintf(stderr, "style-4k: no GPU backend or adapter: %s\n", why.c_str()); return 4; }
    const gpu_check::StyleDispatchCheck r = gpu_check::styleDispatchCheck(*dev, w, h);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
}  // namespace raw
