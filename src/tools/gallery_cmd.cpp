// raw_native_cli material-gallery: see raw/tools/gallery_cmd.hpp.
#include "raw/tools/gallery_cmd.hpp"
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/colour.hpp"
#include "raw/renderer/lighting_parity.hpp"
#include "raw/core/image.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
namespace raw {
namespace {
using gallery::Rgb;
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// A PNG with stored (uncompressed) deflate blocks: no dependency, any viewer reads it.
std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    for (std::size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}
void be32(std::string& s, std::uint32_t v) { for (int k = 3; k >= 0; --k) s.push_back(char((v >> (8 * k)) & 0xFF)); }
void chunk(std::string& out, const char* type, const std::string& data) {
    be32(out, std::uint32_t(data.size()));
    std::string td = std::string(type, 4) + data;
    out += td;
    be32(out, crc32(reinterpret_cast<const std::uint8_t*>(td.data()), td.size()) ^ 0xFFFFFFFFu);
}
bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgb) {
    std::string raw, z = "\x78\x01", out = "\x89PNG\r\n\x1a\n", ihdr;
    for (int y = 0; y < h; ++y) { raw.push_back('\0'); raw.append(reinterpret_cast<const char*>(rgb.data()) + std::size_t(y) * w * 3, std::size_t(w) * 3); }
    std::uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) { a = (a + ch) % 65521u; b = (b + a) % 65521u; }
    for (std::size_t i = 0; i < raw.size(); i += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - i);
        z.push_back(char(i + n >= raw.size() ? 1 : 0));
        z.push_back(char(n & 0xFF)); z.push_back(char(n >> 8)); z.push_back(char(~n & 0xFF)); z.push_back(char((~n >> 8) & 0xFF));
        z.append(raw, i, n);
    }
    be32(z, (b << 16) | a);
    be32(ihdr, std::uint32_t(w)); be32(ihdr, std::uint32_t(h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);
    chunk(out, "IHDR", ihdr); chunk(out, "IDAT", z); chunk(out, "IEND", "");
    std::ofstream f(path, std::ios::binary);
    f << out;
    return bool(f);
}
// Exposed linear radiance to display: AgX into sRGB, then 8 bits; and the linear PFM.
bool writeImages(const std::string& stem, int w, int h, const std::vector<Rgb>& img) {
    colour::Pipeline pl;
    colour::parsePipeline("agx/srgb", pl);
    const colour::Transform tf(pl);
    std::vector<std::uint8_t> rgb(std::size_t(w) * h * 3);
    Buffer<Vec3> pfm; pfm.resize(w, h);
    for (std::size_t i = 0; i < img.size(); ++i) {
        const colour::RGB d = tf.apply({float(img[i].r), float(img[i].g), float(img[i].b)});
        for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = std::uint8_t(std::lround(std::clamp(d[std::size_t(k)], 0.0f, 1.0f) * 255.0f));
        pfm.px[i] = {float(img[i].r), float(img[i].g), float(img[i].b)};
    }
    writePFM(pfm, stem + ".pfm");
    return writePng(stem + ".png", w, h, rgb);
}
}  // namespace

int galleryCommand(int argc, char** argv) {
    std::string out = "gallery";
    int size = 512, spp = 2;
    bool gpu = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--spp") && i + 1 < argc) spp = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cpu-only")) gpu = false;
        else { std::printf("usage: raw_native_cli material-gallery [--out DIR] [--size N] [--spp K] [--cpu-only]\n"); return 2; }
    }
    if (size < 16 || size > 4096 || spp < 1 || spp > 8) { std::printf("material-gallery: --size 16..4096, --spp 1..8\n"); return 2; }
    std::filesystem::create_directories(out);
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    const auto t0 = Clock::now();
    gallery::Gallery g = gallery::build(size, size, spp);
    if (std::getenv("RAW_NATIVE_GALLERY_NOLIGHTS")) g.lights.clear();   // diagnosis: image lighting alone
    const pbr::Tables tables;
    lighting::Cube pre = g.env;
    lighting::prefilter(pre, lighting::kPrefilterSamples);
    const std::vector<Rgb> cpu = gallery::shadeCpu(g, tables, pre, threads);
    const double cpuMs = msSince(t0);
    if (!writeImages(out + "/gallery-cpu", size, size, gallery::resolve(g, cpu))) { std::printf("material-gallery: cannot write %s\n", out.c_str()); return 2; }
    std::string gpuJson = "null", why;
    if (gpu) {
        rhi::Device* dev = rhi::device(why);
        std::vector<float> gs;
        const auto t1 = Clock::now();
        if (dev && gpu_check::shadeOnGpu(*dev, g.grid, g.samples, g.lights, g.env, g.sh, g.exposure, gs, why)) {
            const double gpuMs = msSince(t1);
            std::vector<Rgb> per(g.samples.size());
            double se = 0.0, worst = 0.0;
            const bool dump = std::getenv("RAW_NATIVE_GALLERY_DUMP") != nullptr;   // diagnosis: samples far from the reference
            for (std::size_t k = 0; k < per.size(); ++k) {
                per[k] = {gs[k * 3], gs[k * 3 + 1], gs[k * 3 + 2]};
                if (dump && std::fabs(per[k].r - cpu[k].r) > 2e-3 * std::fabs(cpu[k].r) + 1e-5) {
                    const lighting::Sample& s = g.samples[k];
                    std::fprintf(stderr, "sample %zu pixel %d n.v %.5f gpu %.6g cpu %.6g rough %.3f irid %.2f\n", k, g.pixel[k],
                                 s.n.x * s.v.x + s.n.y * s.v.y + s.n.z * s.v.z, per[k].r, cpu[k].r, s.m.roughness, s.m.iridescence);
                }
                for (double d : {per[k].r - cpu[k].r, per[k].g - cpu[k].g, per[k].b - cpu[k].b}) { se += d * d; worst = std::max(worst, std::fabs(d)); }
            }
            writeImages(out + "/gallery-gpu", size, size, gallery::resolve(g, per));
            char b[512];
            std::snprintf(b, sizeof b, "{\"backend\": \"%s\", \"adapter\": \"%s\", \"wall_ms\": %.1f, \"rmse_exposed\": %.3e, \"max_abs_exposed\": %.3e}",
                          dev->backendName(), dev->adapter().description.c_str(), gpuMs, std::sqrt(se / (3.0 * double(per.size()) + 1e-300)), worst);
            gpuJson = b;
        } else {
            gpuJson = "{\"error\": \"" + why + "\"}";
        }
    }
    std::string rows;
    for (int r = 0; r < gallery::kRows; ++r) { std::string l; gallery::sphereMaterial(r, 0, &l); rows += std::string(r ? ", " : "") + "\"" + l + "\""; }
    std::ofstream j(out + "/gallery.json", std::ios::binary);
    j << "{\n \"what\": \"raw-native material gallery (M3)\",\n \"size\": " << size << ",\n \"spp\": " << spp * spp << ",\n \"samples\": " << g.samples.size()
      << ",\n \"ev100\": " << gallery::kEv100 << ",\n \"display\": \"agx/srgb\",\n \"rows\": [" << rows << "],\n \"cpu_wall_ms\": " << cpuMs
      << ",\n \"gpu\": " << gpuJson << "\n}\n";
    std::printf("material-gallery: %s (%dx%d, %d samples a pixel)\n", out.c_str(), size, size, spp * spp);
    return 0;
}
}  // namespace raw
