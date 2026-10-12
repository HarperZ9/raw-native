// raw_native_cli rr-render (raw/tools/rr_cmd.hpp).
#include "raw/tools/rr_cmd.hpp"
#include "rr_scenes.hpp"
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/rhi/rhi.hpp"
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
struct Args {
    std::string scene, out{"rr-out"};
    std::uint64_t path{0};
    int frames{0}, first{0}, width{480}, height{270};
    std::uint32_t spp{4096}, chunk{1024};
    bool cpu{false};
};
bool parse(int argc, char** argv, Args& a){
    for (int i = 1; i < argc; ++i){
        const bool more = i + 1 < argc;
        const char* k = argv[i];
        if (!std::strcmp(k, "--scene") && more) a.scene = argv[++i];
        else if (!std::strcmp(k, "--path") && more) a.path = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(k, "--frames") && more) a.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--first") && more) a.first = std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--width") && more) a.width = std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--height") && more) a.height = std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--spp") && more) a.spp = (std::uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--chunk") && more) a.chunk = (std::uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(k, "--out") && more) a.out = argv[++i];
        else if (!std::strcmp(k, "--cpu")) a.cpu = true;
        else return false;
    }
    return !a.scene.empty() && a.frames > 0 && a.first >= 0 && a.first < a.frames && a.width >= 8 && a.height >= 8 &&
           a.spp >= 1 && a.chunk >= 1 && a.spp % a.chunk == 0;
}
float lum(const float* c){ return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
// The camera basis the path tracer uses (src/renderer/rt_pt_cpu.cpp camOf and dirOf).
struct Basis { Vec3 eye, f, s, u; float th, aspect; };
Basis basis(const rt::PtCamera& c, int w, int h){
    Basis b;
    b.eye = c.eye; b.f = normalize(c.target - c.eye); b.s = normalize(cross(b.f, c.up)); b.u = cross(b.s, b.f);
    b.th = std::tan(c.fovy * 0.5f); b.aspect = (float)w / (float)h;
    return b;
}
std::uint64_t fnv(const std::string& t){ std::uint64_t h = 1469598103934665603ull; for (unsigned char c : t){ h ^= c; h *= 1099511628211ull; } return h; }
std::string num(double v){ char b[40]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string camJson(const rt::PtCamera& c){
    return "{\"eye\":[" + num(c.eye.x) + "," + num(c.eye.y) + "," + num(c.eye.z) + "],\"target\":[" + num(c.target.x) + "," +
           num(c.target.y) + "," + num(c.target.z) + "],\"up\":[" + num(c.up.x) + "," + num(c.up.y) + "," + num(c.up.z) +
           "],\"fovy\":" + num(c.fovy) + "}";
}
bool writeRaw(const std::string& p, const void* d, std::size_t bytes){
    std::ofstream f(p, std::ios::binary);
    f.write(static_cast<const char*>(d), (std::streamsize)bytes);
    return bool(f);
}
rt::PathTraceOutput trace(rhi::Device* dev, const rt::PtScene& s, const rt::PathTraceDesc& d){
    return dev ? gpu_check::pathTraceGpu(*dev, s, d) : rt::pathTraceCpu(s, d);
}
struct Reference { std::vector<float> ref; double meanVar{0}; std::size_t under1{0}; };
// The reference in chunks of C samples from sample 1, combined by sums over luminance.
bool reference(rhi::Device* dev, const rt::PtScene& s, rt::PathTraceDesc d, std::uint32_t spp, std::uint32_t chunk, std::size_t n, Reference& R){
    std::vector<double> rad(n * 3, 0.0), alb(n * 3, 0.0), S(n, 0.0), Q(n, 0.0);
    for (std::uint32_t b = 0; b < spp; b += chunk){
        d.spp = chunk; d.sppBegin = 1 + b;
        const rt::PathTraceOutput o = trace(dev, s, d);
        if (!o.error.empty()){ std::printf("rr-render: %s\n", o.error.c_str()); return false; }
        const double c = chunk;
        for (std::size_t p = 0; p < n; ++p){
            for (int i = 0; i < 3; ++i){ rad[p * 3 + i] += o.radiance[p * 3 + i]; alb[p * 3 + i] += o.albedo[p * 3 + i]; }
            const double m = lum(&o.radiance[p * 3]), s2 = (double)o.variance[p] * c;
            S[p] += c * m; Q[p] += (c - 1) * s2 + c * m * m;
        }
    }
    const double chunks = (double)(spp / chunk), N = spp;
    R.ref.assign(n * 7, 0.0f);
    for (std::size_t p = 0; p < n; ++p){
        for (int i = 0; i < 3; ++i){ R.ref[p * 7 + i] = (float)(rad[p * 3 + i] / chunks); R.ref[p * 7 + 4 + i] = (float)(alb[p * 3 + i] / chunks); }
        const double var = N > 1 ? std::fmax(0.0, (Q[p] - S[p] * S[p] / N) / (N - 1)) / N : 0.0;
        R.ref[p * 7 + 3] = (float)var;
        R.meanVar += var;
        const double m = S[p] / N;
        if ((m <= 0 && var <= 0) || (m > 0 && std::sqrt(var) < 0.01 * m)) ++R.under1;
    }
    R.meanVar /= (double)n;
    return true;
}
// The input frame's 17 channels.
std::vector<float> inputChannels(const rt::PtScene& s, const rt::PathTraceOutput& o, const rt::PtCamera& cam, const rt::PtCamera& prev){
    const int W = o.width, H = o.height;
    const std::size_t n = (std::size_t)W * (std::size_t)H;
    std::vector<float> rough, metal, x(n * 17, 0.0f);
    rr::materialAovs(s, o.triangle, rough, metal);
    const Basis b = basis(cam, W, H), pb = basis(prev, W, H);
    for (int y = 0; y < H; ++y) for (int xx = 0; xx < W; ++xx){
        const std::size_t p = (std::size_t)y * (std::size_t)W + (std::size_t)xx;
        float* c = &x[p * 17];
        for (int k = 0; k < 3; ++k){ c[k] = o.radiance[p * 3 + k]; c[3 + k] = o.albedo[p * 3 + k]; c[6 + k] = o.normal[p * 3 + k]; }
        c[9] = o.depth[p]; c[10] = o.motion[p * 2]; c[11] = o.motion[p * 2 + 1];
        c[12] = (float)xx + 0.5f + c[10]; c[13] = (float)y + 0.5f + c[11];
        if (o.depth[p] > 0){
            const float nx = ((float)xx + 0.5f) / (float)W * 2 - 1, ny = 1 - ((float)y + 0.5f) / (float)H * 2;
            const Vec3 d = normalize(b.f + b.s * (nx * b.th * b.aspect) + b.u * (ny * b.th));
            const Vec3 P = b.eye + d * (o.depth[p] / dot(d, b.f));
            c[14] = dot(P - pb.eye, pb.f);
        }
        c[15] = rough[p]; c[16] = metal[p];
    }
    return x;
}
}  // namespace

int rrRenderCommand(int argc, char** argv){
    Args a;
    if (!parse(argc, argv, a)){
        std::printf("usage: raw_native_cli rr-render --scene NAME --path SEED --frames N [--first K] [--width W] [--height H]\n"
                    "         [--spp S] [--chunk C] [--out DIR] [--cpu]   (S a multiple of C)\n");
        return 2;
    }
    rt::PtScene s;
    rt::PtCamera base;
    std::string lighting;
    if (!rr::scene(a.scene, s, base, lighting)){ std::printf("rr-render: unknown scene %s\n", a.scene.c_str()); return 2; }
    std::error_code ec;
    std::filesystem::create_directories(a.out, ec);
    std::string why;
    rhi::Device* dev = a.cpu ? nullptr : rhi::device(why);
    if (!a.cpu && !dev){ std::printf("{\"error\": \"no GPU: %s\"}\n", why.c_str()); return 4; }
    const std::size_t n = (std::size_t)a.width * (std::size_t)a.height;
    for (int k = a.first; k < a.frames; ++k){
        const rt::PtCamera cam = rr::pathCamera(base, a.path, k, a.frames), prev = rr::pathCamera(base, a.path, k - 1, a.frames);
        rt::PathTraceDesc d;
        d.width = a.width; d.height = a.height; d.camera = cam; d.previous = prev;
        d.seed = rr::mix(fnv(a.scene) ^ (a.path * 1000003ull) ^ (std::uint64_t)(k + 1));
        auto t0 = std::chrono::steady_clock::now();
        d.spp = 1; d.sppBegin = 0;
        const rt::PathTraceOutput in = trace(dev, s, d);
        const double msIn = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!in.error.empty()){ std::printf("rr-render: %s\n", in.error.c_str()); return 2; }
        t0 = std::chrono::steady_clock::now();
        Reference R;
        if (!reference(dev, s, d, a.spp, a.chunk, n, R)) return 2;
        const double msRef = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const std::vector<float>& ref = R.ref;
        const double meanVar = R.meanVar;
        const std::size_t under1 = R.under1;
        const std::vector<float> x = inputChannels(s, in, cam, prev);
        char stem[64];
        std::snprintf(stem, sizeof stem, "/f%03d", k);
        const std::string P = a.out + stem;
        bool ok = writeRaw(P + "_in.f32", x.data(), x.size() * 4) && writeRaw(P + "_tri.i32", in.triangle.data(), n * 4) &&
                  writeRaw(P + "_ref.f32", ref.data(), ref.size() * 4);
        const std::string j = "{\"scene\":\"" + a.scene + "\",\"lighting\":\"" + lighting + "\",\"path\":" + std::to_string(a.path) +
            ",\"frame\":" + std::to_string(k) + ",\"frames\":" + std::to_string(a.frames) + ",\"width\":" + std::to_string(a.width) +
            ",\"height\":" + std::to_string(a.height) + ",\"seed\":" + std::to_string(d.seed) + ",\"input_spp\":1,\"ref_spp\":" +
            std::to_string(a.spp) + ",\"ref_chunk\":" + std::to_string(a.chunk) + ",\"camera\":" + camJson(cam) + ",\"previous\":" +
            camJson(prev) + ",\"interface_version\":" + std::to_string(rt::kPathTraceVersion) + ",\"backend\":\"" +
            (dev ? dev->backendName() : std::string("cpu")) + "\",\"adapter\":\"" + (dev ? dev->adapter().description : std::string("")) +
            "\",\"ms_input\":" + num(msIn) + ",\"ms_ref\":" + num(msRef) + ",\"ref_mean_var_of_mean\":" + num(meanVar) +
            ",\"ref_fraction_se_under_1pct\":" + num((double)under1 / (double)n) + "}\n";
        ok = ok && writeRaw(P + ".json", j.data(), j.size());
        if (!ok){ std::printf("rr-render: cannot write %s\n", P.c_str()); return 2; }
        std::printf("%s", j.c_str());
        std::fflush(stdout);
    }
    return 0;
}
}  // namespace raw
