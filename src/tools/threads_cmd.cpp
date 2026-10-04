#include "raw/tools/threads_cmd.hpp"
#include "raw/renderer/threads.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace raw {
namespace {
const char* kThreadsUsage =
    "usage: raw_native_cli threads [--out <dir>] [--world 0..14] [--frames n] [--width w] [--height h]\n"
    "         [--particles n] [--substeps n] [--fps f] [--persistence f] [--exposure f]\n"
    "         [--spacing f] [--levels f]\n"
    "writes <dir>/threads.ppm, the last of <frames> frames, and prints the time per frame\n";
bool parse(int argc, char** argv, threads::Settings& s, std::string& out){
    for (int i = 1; i < argc; ++i){
        const std::string a = argv[i];
        if (i + 1 >= argc) return false;
        const char* v = argv[++i];
        if (a == "--out") out = v;
        else if (a == "--world") s.world = std::atoi(v);
        else if (a == "--frames") s.frames = std::atoi(v);
        else if (a == "--width") s.width = std::atoi(v);
        else if (a == "--height") s.height = std::atoi(v);
        else if (a == "--particles") s.particles = (uint32_t)std::strtoul(v, nullptr, 10);
        else if (a == "--substeps") s.substeps = (uint32_t)std::strtoul(v, nullptr, 10);
        else if (a == "--fps") s.fps = (float)std::atof(v);
        else if (a == "--persistence") s.persistence = (float)std::atof(v);
        else if (a == "--exposure") s.exposure = (float)std::atof(v);
        else if (a == "--spacing") s.spacing = (float)std::atof(v);
        else if (a == "--levels") s.levels = (float)std::atof(v);
        else return false;
    }
    return true;
}
}
int threadsCommand(int argc, char** argv){
    threads::Settings s;
    std::string out = ".", err;
    if (!parse(argc, argv, s, out)){ std::printf("%s", kThreadsUsage); return 2; }
    rhi::Device* dev = rhi::device(err);
    if (!dev){ std::printf("threads: no GPU (%s backend): %s\n", rhi::linkedBackend(), err.c_str()); return 4; }
    threads::Result r;
    if (!threads::render(*dev, s, r, err)){ std::printf("threads: %s\n", err.c_str()); return 2; }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    std::ofstream f(out + "/threads.ppm", std::ios::binary);
    f << "P6\n" << r.width << " " << r.height << "\n255\n";
    for (size_t i = 0; i < r.rgba.size(); i += 4) f.write((const char*)&r.rgba[i], 3);
    if (!f){ std::printf("threads: could not write %s/threads.ppm\n", out.c_str()); return 2; }
    std::printf("threads: %s on %s, %dx%d, %u particles, world %d, %d frames, %.3f ms per frame\n",
        rhi::linkedBackend(), dev->adapter().description.c_str(), r.width, r.height, s.particles, s.world, s.frames, r.msPerFrame);
    return 0;
}
}
