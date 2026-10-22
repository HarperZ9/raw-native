// raw_native_cli art: see raw/tools/art_cmd.hpp.
#include "raw/tools/art_cmd.hpp"
#include "art_report.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
namespace raw {
namespace {
int usage() {
    std::printf("usage: raw_native_cli art strokes [--out DIR] [--threads N] [--frames N] [--no-orbit] [--gpu] [--cost] [--repeats R]\n");
    return 2;
}
}  // namespace

int artCommand(int argc, char** argv) {
    if (argc < 2) return usage();
    art_cmd::Options o;
    o.feature = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) o.out = argv[++i];
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) o.threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--repeats") && i + 1 < argc) o.repeats = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-orbit")) o.orbit = false;
        else if (!std::strcmp(argv[i], "--gpu")) o.gpu = true;
        else if (!std::strcmp(argv[i], "--cost")) o.cost = true;
        else return usage();
    }
    if (o.threads < 1 || o.frames < 12 || o.repeats < 1) return usage();
    std::error_code ec;
    std::filesystem::create_directories(o.out, ec);
    if (ec) { std::printf("art: cannot create %s\n", o.out.c_str()); return 2; }
    if (o.feature == "strokes") return art_cmd::strokes(o);
    return usage();
}
}  // namespace raw
