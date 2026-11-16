// raw_native_cli art enbcrt: the ENB CRT and VHS port's record against evidence/art-enb-crt-bounds.json
// (e1 to e5).
#include "art_report.hpp"
#include "raw/renderer/art_enbcrt.hpp"
#include "raw/renderer/art_orbit.hpp"
#include "raw/renderer/art_strokes.hpp"
#include "raw/rhi/rhi.hpp"
#include <algorithm>
#include <cstdio>
namespace raw::art_cmd {
namespace {
const char* const kGpuPresets[] = {"crt", "crt-slot", "crt-shadow", "vhs", "vhs-crt", "vhs-ep"};
constexpr double kExposure = 1.6;

std::vector<float> display(const art::Frame& f) {
    const std::vector<std::uint8_t> c = art::paintReference(f.lit, kExposure);
    std::vector<float> o(c.size());
    for (std::size_t i = 0; i < c.size(); ++i) o[i] = float(c[i]) / 255.0f;
    return o;
}
// A preset's frame on the CPU; vhs-then-crt chains the VHS pass into the CRT pass.
std::vector<std::uint8_t> cpuCodes(const std::string& preset, const std::vector<float>& img, int w, int h, int frame) {
    if (preset == "vhs-then-crt")
        return art::codes8Display(art::runEnbCrt(art::enbCrtPreset("crt"), art::runEnbCrt(art::enbCrtPreset("vhs"), img, w, h, frame), w, h, frame));
    return art::codes8Display(art::runEnbCrt(art::enbCrtPreset(preset), img, w, h, frame));
}

std::string goldens(const Options& o, bool& pass) {
    const art::Frame f = art::renderFrame(art::orbitView(0, 256, 144), art::daylight(), o.threads);
    const std::vector<float> img = display(f);
    const std::vector<std::uint8_t> in = art::codes8Display(img);
    Json j;
    writeCodesPng(o.out + "/input.png", f.w, f.h, in);
    for (const char* p : {"off", "crt-zero", "crt", "crt-slot", "crt-shadow", "vhs", "vhs-crt", "vhs-ep", "vhs-then-crt"}) {
        const auto c = cpuCodes(p, img, f.w, f.h, 0);
        j.str(p, hex(art::fnv1a(c)));
        if (std::string(p) == "off" || std::string(p) == "crt-zero") { const bool same = c == in; j.boolean((std::string("e3_") + p + "_identity").c_str(), same); pass = pass && same; }
        else writeCodesPng(o.out + "/golden-" + p + ".png", f.w, f.h, c);
    }
    return j.done();
}

std::string gpuCase(rhi::Device& dev, const Options& o, int frame, const std::string& preset, bool& pass) {
    const art::Frame f = art::renderFrame(art::orbitView(frame, 256, 144), art::daylight(), o.threads);
    const std::vector<float> img = display(f);
    const art::EnbCrtParams p = art::enbCrtPreset(preset);
    std::vector<float> bright = img;   // the control: the input 5% brighter
    for (std::size_t i = 0; i < bright.size(); ++i) if ((i & 3) != 3) bright[i] = std::min(1.0f, bright[i] * 1.05f);
    const auto want = cpuCodes(preset, img, f.w, f.h, frame);
    const auto wantBent = cpuCodes(preset, bright, f.w, f.h, frame);
    gpu_check::EnbCrtGpu g;
    Json j;
    j.integer("frame", frame).str("preset", preset);
    if (!gpu_check::enbCrtOnGpu(dev, p, img, f.w, f.h, frame, g, 0)) { pass = false; return j.str("error", g.error).done(); }
    const CodeStats s = codeStats(want, unpack(g.packed)), c = codeStats(wantBent, unpack(g.packed));
    auto within = [](const CodeStats& x) { return x.mean <= 0.1 && x.p999 <= 2 && double(x.over2) <= 5e-4 * double(x.n); };
    const bool ok = within(s) && !within(c);
    pass = pass && ok;
    if (frame == 0) writeCodesPng(o.out + "/gpu-" + preset + ".png", f.w, f.h, unpack(g.packed));
    return j.raw("codes", statsJson(s)).raw("control", statsJson(c)).boolean("control_fails", !within(c)).boolean("pass", ok).done();
}

std::string temporal(const Options& o, bool& pass) {
    Json j;
    for (double step : {1.0, 0.0}) {
        const art::OrbitSeq s = art::renderOrbitSeq(320, 180, o.frames, step, art::daylight(), o.threads);
        std::vector<std::vector<std::uint8_t>> ref;
        std::vector<std::vector<float>> img;
        for (const art::Frame& f : s.frames) { img.push_back(display(f)); ref.push_back(art::codes8Display(img.back())); }
        for (const char* p : {"crt", "vhs", "vhs-crt"}) {
            std::vector<std::vector<std::uint8_t>> out;
            for (std::size_t t = 0; t < img.size(); ++t) out.push_back(cpuCodes(p, img[t], 320, 180, int(t)));
            const std::vector<double> v = art::instabilitySeries(s, out, ref, 9);
            double mean = 0, worst = 0;
            for (double x : v) { mean += x / double(v.size()); worst = std::max(worst, x); }
            const std::string key = std::string(step > 0 ? "orbit_" : "static_") + p;
            if (step == 0 && std::string(p) == "crt") { j.boolean("e4_static_crt_zero", worst == 0.0); pass = pass && worst == 0.0; }
            j.raw(key.c_str(), Json().num("mean", mean).num("max", worst).nums("series", v).done());
            std::printf("art enbcrt: %s instability %.5f\n", key.c_str(), mean);
        }
    }
    return j.done();
}

std::string cost(rhi::Device& dev, const Options& o, bool& pass) {
    const art::Frame f = art::renderFrame(art::orbitView(0, 1920, 1080), art::daylight(), o.threads);
    gpu_check::EnbCrtGpu g;
    Json j;
    j.integer("width", 1920).integer("height", 1080).integer("repeats", o.repeats).integer("inner_repeats", 10).str("preset", "vhs-crt");
    if (!gpu_check::enbCrtOnGpu(dev, art::enbCrtPreset("vhs-crt"), display(f), f.w, f.h, 0, g, o.repeats)) { pass = false; return j.str("error", g.error).done(); }
    pass = pass && g.ms <= 1.0;
    return j.num("ms", g.ms).boolean("e5_pass", g.ms <= 1.0).done();
}
}  // namespace

int enbcrt(const Options& o) {
    bool pass = true;
    Json j;
    j.str("schema", "raw-native.evidence/1").str("bounds", "evidence/art-enb-crt-bounds.json").raw("goldens", goldens(o, pass));
    if (o.orbit) j.raw("temporal", temporal(o, pass));
    if (o.gpu || o.cost) {
        std::string why;
        rhi::Device* dev = rhi::device(why);
        if (!dev) { std::fprintf(stderr, "art enbcrt: no GPU backend or adapter: %s\n", why.c_str()); writeText(o.out + "/enbcrt.json", j.done()); return 4; }
        j.str("backend", dev->backendName()).str("adapter", dev->adapter().description);
        if (o.gpu) {
            std::string cases = "[";
            for (int frame : {0, 6}) for (const char* p : kGpuPresets) cases += (cases.size() > 1 ? "," : "") + gpuCase(*dev, o, frame, p, pass);
            j.raw("gpu_cases", cases + "]");
        }
        if (o.cost) j.raw("cost", cost(*dev, o, pass));
    }
    j.boolean("pass", pass);
    const std::string doc = j.done();
    if (!writeText(o.out + "/enbcrt.json", doc)) return 2;
    std::printf("%s\n", doc.c_str());
    return pass ? 0 : 1;
}

}  // namespace raw::art_cmd
