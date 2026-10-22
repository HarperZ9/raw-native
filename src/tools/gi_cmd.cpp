// raw_native_cli gi: lighting model 3 (model 2 plus split radiance cascades for the diffuse
// indirect term) on a scene, from the CPU reference and optionally the GPU (ROADMAP P6;
// evidence/p6-radiance-cascades-bounds.json).
//
//   raw_native_cli gi [scene flags] --out DIR [--frames F] [--warmup W] [--stride S] [--gpu]
//                     [--no-cpu] [--time N] [--threads T]
//
// DIR gets frame_hdr.pfm (model 3, from the CPU reference, or from the GPU with --no-cpu) and
// mask.pgm, which pt-error reads, indirect.pfm, frame.ppm and gi.json. RAW_NATIVE_GI_CONTROL=nomerge
// runs the GPU's test-only control merge. Exit 0 when the run finished, 2 on bad input, 4 when
// --gpu finds no adapter.
#include "raw/tools/gi_cmd.hpp"
#include "raw/renderer/gi_gpu.hpp"
#include "raw/renderer/accel.hpp"
#include "raw/renderer/composite.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/ray_ao.hpp"
#include "raw/renderer/render.hpp"
#include "raw/core/image.hpp"
#include "raw/core/sha256.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/tools/model_scene.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
struct Opts { gi::Params p; bool gpu{false}, cpu{true}; int time{0}; };
bool parseOpts(int argc, char** argv, Opts& o, std::vector<const char*>& rest){
    rest = {argv[0]};
    for (int i = 1; i < argc; ++i){
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--frames" && more) o.p.frames = std::atoi(argv[++i]);
        else if (a == "--warmup" && more) o.p.warmup = std::atoi(argv[++i]);
        else if (a == "--stride" && more) o.p.stride = std::atoi(argv[++i]);
        else if (a == "--time" && more) o.time = std::atoi(argv[++i]);
        else if (a == "--gpu") o.gpu = true;
        else if (a == "--no-cpu") o.cpu = false;
        else rest.push_back(argv[i]);
    }
    return o.p.frames > 0 && o.p.warmup >= 0 && o.p.warmup < o.p.frames && o.p.stride >= 1 && (o.cpu || o.gpu);
}
std::string hashOf(const std::vector<Vec3>& v){ return sha256Hex(v.data(), v.size() * sizeof(Vec3)); }
// Relative RMSE of b against a over covered pixels (luminance), and the worst absolute channel difference.
void compare(const GBuffer& g, const std::vector<Vec3>& a, const std::vector<Vec3>& b, double& rel, double& worst){
    double s2 = 0, sa = 0; long n = 0; worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i){
        if (!g.mask.px[i]) continue;
        const double la = 0.2126 * a[i].x + 0.7152 * a[i].y + 0.0722 * a[i].z, lb = 0.2126 * b[i].x + 0.7152 * b[i].y + 0.0722 * b[i].z;
        s2 += (la - lb) * (la - lb); sa += la; ++n;
        for (float d : {a[i].x - b[i].x, a[i].y - b[i].y, a[i].z - b[i].z}) worst = std::fmax(worst, std::fabs(double(d)));
    }
    rel = (n && sa > 0) ? std::sqrt(s2 / double(n)) / (sa / double(n)) : 0.0;
}
std::vector<Vec3> pixelsOf(const Buffer<Vec3>& b){ return std::vector<Vec3>(b.px.begin(), b.px.end()); }
Buffer<Vec3> bufferOf(const std::vector<Vec3>& v, int w, int h){ Buffer<Vec3> b; b.resize(w, h); for (std::size_t i = 0; i < v.size(); ++i) b.px[i] = v[i]; return b; }
float maxOf(const std::vector<Vec3>& v){ float m = 0.0f; for (Vec3 c : v) m = std::fmax(m, std::fmax(c.x, std::fmax(c.y, c.z))); return m; }

// The frame every lighting model shares: the scene, its G-buffer and model 2's visibility.
struct Frame { Scene s; GBuffer g; Buffer<float> sky, sun; std::vector<Vec3> model2; };
Frame frameOf(const CliParams& p){
    Frame f{sceneFromParams(p, nullptr), {}, {}, {}, {}};
    f.s.camera = cameraFromParams(p);
    f.g = rasterize(f.s, p.width, p.height);
    LinearAccel accel; accel.build(f.s);
    Buffer<float> ao;
    const Vec3 toLight = (f.s.lights.empty() ? Vec3{0, -1, 0} : f.s.lights[0].dir) * -1.0f;
    computeRTVisibility(f.g, accel, kRtSamples, kAoRadius, toLight, ao, f.sky, f.sun, p.threads);
    f.model2 = pixelsOf(shadeHDRV2(f.g, f.sky, f.sun, f.s));
    return f;
}
std::string cpuJson(const Frame& f, const gi::Result& r, double ms){
    const std::vector<Vec3> m3 = pixelsOf(gi::shadeModel3(f.g, f.sky, f.sun, f.s, r.indirect));
    const bool same = m3.size() == f.model2.size() && std::memcmp(m3.data(), f.model2.data(), m3.size() * sizeof(Vec3)) == 0;
    char b[700];
    std::snprintf(b, sizeof b, " \"cpu\": {\"ms\": %.1f, \"indirect_sha256\": \"%s\", \"indirect_max\": %.6g, \"model3_equals_model2\": %s,\n"
                  "  \"cache_entries\": %llu, \"receiver_rays\": %llu, \"slots\": [%u, %u, %u, %u]},\n",
                  ms, hashOf(r.indirect).c_str(), double(maxOf(r.indirect)), same ? "true" : "false",
                  (unsigned long long)r.cacheEntries, (unsigned long long)r.raysTraced, r.slots[0], r.slots[1], r.slots[2], r.slots[3]);
    return b;
}
std::string gpuJson(const Frame& f, const gi::GpuRun& r, const gi::Result* cpu, bool control){
    double relI = 0, worstI = 0, relF = 0, worstF = 0;
    if (cpu){
        compare(f.g, cpu->indirect, r.indirect, relI, worstI);
        compare(f.g, pixelsOf(gi::shadeModel3(f.g, f.sky, f.sun, f.s, cpu->indirect)), pixelsOf(gi::shadeModel3(f.g, f.sky, f.sun, f.s, r.indirect)), relF, worstF);
    }
    char b[900];
    std::snprintf(b, sizeof b, " \"gpu\": {\"backend\": \"%s\", \"adapter\": \"%s\", \"control\": %s, \"slots\": [%u, %u, %u, %u], \"slots_match\": %s,\n"
                  "  \"indirect_max\": %.6g, \"indirect_rel_rmse\": %.6g, \"indirect_max_abs\": %.6g, \"frame_rel_rmse\": %.6g, \"frame_max_abs\": %.6g,\n"
                  "  \"frame_ms\": %.4f, \"repeats\": %d},\n",
                  r.backend.c_str(), r.adapter.c_str(), control ? "true" : "false", r.slots[0], r.slots[1], r.slots[2], r.slots[3],
                  r.slotsMatch ? "true" : "false", double(maxOf(r.indirect)), relI, worstI, relF, worstF, r.frameMs, r.repeats);
    return b;
}
void writeOutputs(const Frame& f, const CliParams& p, const std::vector<Vec3>& indirect, std::string& json){
    const Buffer<Vec3> frame = gi::shadeModel3(f.g, f.sky, f.sun, f.s, indirect);
    writePFM(frame, p.out + "/frame_hdr.pfm");
    writePFM(bufferOf(indirect, p.width, p.height), p.out + "/indirect.pfm");
    writePPM(tonemapReinhard(frame), p.out + "/frame.ppm");
    writeMaskPGM(f.g.mask, p.out + "/mask.pgm");
    json += " \"frame_hdr_sha256\": \"" + sha256File(p.out + "/frame_hdr.pfm") + "\"\n}\n";
    std::ofstream(p.out + "/gi.json", std::ios::binary) << json;
    std::printf("%s", json.c_str());
}
}  // namespace

int giCommand(int argc, char** argv){
    Opts o;
    std::vector<const char*> rest;
    const bool ok = parseOpts(argc, argv, o, rest);
    std::string err;
    const auto cp = parseArgs(int(rest.size()), rest.data(), err);
    if (!ok || !cp || cp->out.empty()){
        std::printf("usage: raw_native_cli gi [scene flags] --out DIR [--frames F] [--warmup W] [--stride S] [--gpu] [--no-cpu] [--time N]%s%s\n",
                    err.empty() ? "" : "\n  ", err.c_str());
        return 2;
    }
    const CliParams& p = *cp;
    const Frame f = frameOf(p);
    const gi::Scene3 s3 = gi::sceneOf(f.s);
    std::filesystem::create_directories(p.out);
    char b[512];
    std::snprintf(b, sizeof b, "{\n \"schema\": \"raw-native.gi/1\",\n \"scene\": \"%s\",\n \"width\": %d, \"height\": %d, \"frames\": %d, \"warmup\": %d, \"stride\": %d, \"triangles\": %zu,\n \"model2_hdr_sha256\": \"%s\",\n",
                  p.ownedScene.empty() ? "test-scene" : p.ownedScene.c_str(), p.width, p.height, o.p.frames, o.p.warmup, o.p.stride, s3.tris.size(), hashOf(f.model2).c_str());
    std::string json = b;
    gi::Result cpu;
    if (o.cpu){
        const auto t0 = std::chrono::steady_clock::now();
        cpu = gi::cascadesCpu(s3, f.g, o.p, p.threads);
        if (!cpu.error.empty()){ std::printf("gi: %s\n", cpu.error.c_str()); return 2; }
        json += cpuJson(f, cpu, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::vector<Vec3> indirect = cpu.indirect;
    if (o.gpu){
        rhi::Device* dev = rhi::device(err);
        if (!dev){ std::printf("gi: no GPU: %s\n", err.c_str()); return 4; }
        const char* ctl = std::getenv("RAW_NATIVE_GI_CONTROL");
        const bool control = ctl && std::strcmp(ctl, "nomerge") == 0;
        const gi::GpuRun r = gi::cascadesGpu(*dev, s3, f.g, o.p, control, o.time);
        if (!r.error.empty()){ std::printf("gi: GPU: %s\n", r.error.c_str()); return 2; }
        json += gpuJson(f, r, o.cpu ? &cpu : nullptr, control);
        if (!o.cpu) indirect = r.indirect;
    }
    writeOutputs(f, p, indirect, json);
    return 0;
}
}  // namespace raw
