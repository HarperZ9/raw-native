// raw_native_cli rr-taa (raw/tools/rr_taa_cmd.hpp).
#include "raw/tools/rr_taa_cmd.hpp"
#include "raw/renderer/taa.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
bool readFloats(const std::string& p, std::vector<float>& v, std::size_t n){
    std::ifstream f(p, std::ios::binary);
    v.resize(n);
    f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * 4));
    return bool(f);
}
// Width and height from the frame's JSON ("width":W, "height":H).
bool size(const std::string& json, int& w, int& h){
    std::ifstream f(json);
    const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto a = s.find("\"width\":"), b = s.find("\"height\":");
    if (a == std::string::npos || b == std::string::npos) return false;
    w = std::atoi(s.c_str() + a + 8); h = std::atoi(s.c_str() + b + 9);
    return w > 0 && h > 0;
}
}  // namespace

int rrTaaCommand(int argc, char** argv){
    std::string seq, in, out;
    for (int i = 1; i + 1 < argc; i += 2){
        if (!std::strcmp(argv[i], "--seq")) seq = argv[i + 1];
        else if (!std::strcmp(argv[i], "--in")) in = argv[i + 1];
        else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
    }
    if (seq.empty() || in.empty() || out.empty()){ std::printf("usage: raw_native_cli rr-taa --seq DIR --in DIR --out DIR\n"); return 2; }
    std::filesystem::create_directories(out);
    std::vector<float> hist, prevDepth;
    int frames = 0;
    for (int k = 0;; ++k){
        char stem[32];
        std::snprintf(stem, sizeof stem, "/f%03d", k);
        int w = 0, h = 0;
        if (!std::filesystem::exists(seq + stem + ".json") || !std::filesystem::exists(in + stem + ".f32")) break;
        if (!size(seq + stem + ".json", w, h)) return 2;
        const std::size_t n = (std::size_t)w * (std::size_t)h;
        std::vector<float> x, c;
        if (!readFloats(seq + stem + "_in.f32", x, n * 17) || !readFloats(in + stem + ".f32", c, n * 3)) return 2;
        taa::Frame f;
        f.w = w; f.h = h; f.rgb = c;
        f.depth.resize(n); f.prev.resize(n * 3);
        for (std::size_t p = 0; p < n; ++p){
            const bool hit = x[p * 17 + 9] > 0;
            f.depth[p] = hit ? x[p * 17 + 9] : -1.0f;
            f.prev[p * 3] = x[p * 17 + 12]; f.prev[p * 3 + 1] = x[p * 17 + 13];
            f.prev[p * 3 + 2] = hit ? x[p * 17 + 14] : -1.0f;
        }
        hist = taa::resolve(f, hist, prevDepth);
        prevDepth = f.depth;
        std::ofstream o(out + stem + ".f32", std::ios::binary);
        o.write(reinterpret_cast<const char*>(hist.data()), std::streamsize(hist.size() * 4));
        if (!o) return 2;
        ++frames;
    }
    std::printf("{\"frames\": %d}\n", frames);
    return frames > 0 ? 0 : 2;
}
}  // namespace raw
