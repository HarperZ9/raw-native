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
    Scene s = sceneFromParams(p, nullptr);
    s.camera = cameraFromParams(p);
    const GBuffer g = rasterize(s, p.width, p.height);
    LinearAccel accel; accel.build(s);
    Buffer<float> ao, sky, sun;
    const Vec3 toLight = (s.lights.empty() ? Vec3{0, -1, 0} : s.lights[0].dir) * -1.0f;
    computeRTVisibility(g, accel, kRtSamples, kAoRadius, toLight, ao, sky, sun, p.threads);
    const std::vector<Vec3> model2 = pixelsOf(shadeHDRV2(g, sky, sun, s));
    const gi::Scene3 s3 = gi::sceneOf(s);
    std::filesystem::create_directories(p.out);
    std::string json = "{\n \"schema\": \"raw-native.gi/1\",\n \"scene\": \"" + (p.ownedScene.empty() ? std::string("test-scene") : p.ownedScene) + "\",\n";
    char b[512];
    std::snprintf(b, sizeof b, " \"width\": %d, \"height\": %d, \"frames\": %d, \"warmup\": %d, \"stride\": %d, \"triangles\": %zu,\n \"model2_hdr_sha256\": \"%s\",\n",
                  p.width, p.height, o.p.frames, o.p.warmup, o.p.stride, s3.tris.size(), hashOf(model2).c_str());
    json += b;
    gi::Result cpu;
    if (o.cpu){
        const auto t0 = std::chrono::steady_clock::now();
        cpu = gi::cascadesCpu(s3, g, o.p, p.threads);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!cpu.error.empty()){ std::printf("gi: %s\n", cpu.error.c_str()); return 2; }
        std::snprintf(b, sizeof b, " \"cpu\": {\"ms\": %.1f, \"indirect_sha256\": \"%s\", \"cache_entries\": %llu, \"receiver_rays\": %llu, \"slots\": [%u, %u, %u, %u]},\n",
                      ms, hashOf(cpu.indirect).c_str(), (unsigned long long)cpu.cacheEntries, (unsigned long long)cpu.raysTraced,
                      cpu.slots[0], cpu.slots[1], cpu.slots[2], cpu.slots[3]);
        json += b;
    }
    std::vector<Vec3> indirect = cpu.indirect;
    if (o.gpu){
        rhi::Device* dev = rhi::device(err);
        if (!dev){ std::printf("gi: no GPU: %s\n", err.c_str()); return 4; }
        const char* ctl = std::getenv("RAW_NATIVE_GI_CONTROL");
        const bool control = ctl && std::strcmp(ctl, "nomerge") == 0;
        const gi::GpuRun r = gi::cascadesGpu(*dev, s3, g, o.p, control, o.time);
        if (!r.error.empty()){ std::printf("gi: GPU: %s\n", r.error.c_str()); return 2; }
        double relI = 0, worstI = 0, relF = 0, worstF = 0;
        if (o.cpu){
            compare(g, cpu.indirect, r.indirect, relI, worstI);
            const std::vector<Vec3> fc = pixelsOf(gi::shadeModel3(g, sky, sun, s, cpu.indirect)), fg = pixelsOf(gi::shadeModel3(g, sky, sun, s, r.indirect));
            compare(g, fc, fg, relF, worstF);
        }
        std::snprintf(b, sizeof b, " \"gpu\": {\"backend\": \"%s\", \"adapter\": \"%s\", \"control\": %s, \"slots\": [%u, %u, %u, %u], \"slots_match\": %s,\n"
                      "  \"indirect_rel_rmse\": %.6g, \"indirect_max_abs\": %.6g, \"frame_rel_rmse\": %.6g, \"frame_max_abs\": %.6g,\n  \"frame_ms\": %.4f, \"repeats\": %d},\n",
                      r.backend.c_str(), r.adapter.c_str(), control ? "true" : "false", r.slots[0], r.slots[1], r.slots[2], r.slots[3],
                      r.slotsMatch ? "true" : "false", relI, worstI, relF, worstF, r.frameMs, r.repeats);
        json += b;
        if (!o.cpu) indirect = r.indirect;
    }
    const Buffer<Vec3> frame = gi::shadeModel3(g, sky, sun, s, indirect);
    writePFM(frame, p.out + "/frame_hdr.pfm");
    writePFM(bufferOf(indirect, p.width, p.height), p.out + "/indirect.pfm");
    writePPM(tonemapReinhard(frame), p.out + "/frame.ppm");
    writeMaskPGM(g.mask, p.out + "/mask.pgm");
    json += " \"frame_hdr_sha256\": \"" + sha256File(p.out + "/frame_hdr.pfm") + "\"\n}\n";
    std::ofstream(p.out + "/gi.json", std::ios::binary) << json;
    std::printf("%s", json.c_str());
    return 0;
}
}  // namespace raw
