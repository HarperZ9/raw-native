// The post stack driver's checks: plans against web/motion/post.mjs, and chains on the host and on
// the device. Bounds and controls of evidence/cpp-port-style-bounds-2.json, post_stack, written
// before the first run.
#include "raw/renderer/post_stack.hpp"
#include "raw/renderer/style_parity.hpp"
#include "post_stack_gpu.hpp"
#include "style_fixture.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gpu_check {
namespace {
using assets::Json;
using post_stack::HostFrame;
using post_stack::Spec;
namespace fx = style_fx;

std::vector<Spec> specsOf(const Json& passes) {
    std::vector<Spec> v;
    for (const Json& p : passes.a) {
        Spec s{p.get("pass")->s, {}};
        for (const auto& [k, x] : p.o) if (k != "pass" && x.is(Json::Kind::Number)) s.num.emplace_back(k, x.n);
        v.push_back(s);
    }
    return v;
}
std::vector<std::string> names(const std::vector<Spec>& v) {
    std::vector<std::string> o;
    for (const Spec& s : v) o.push_back(s.pass);
    return o;
}
std::vector<std::string> names(const Json& a) {
    std::vector<std::string> o;
    for (const Json& s : a.a) o.push_back(s.s);
    return o;
}
bool samePlan(const post_stack::Plan& p, const Json& js) {
    if (p.refused != js.get("refused")->b) return false;
    return p.refused || (names(p.scene) == names(*js.get("scene")) && names(p.display) == names(*js.get("display")));
}
// One f32 ulp at |b| (the spacing above it).
double ulpsApart(float a, float b) {
    const float m = std::abs(b), sp = std::nextafter(m, INFINITY) - m;
    return std::abs(double(a) - double(b)) / double(sp);
}
bool runHost(const std::vector<Spec>& specs, const HostFrame& in, int frames, std::vector<HostFrame>& out, std::string& err) {
    post_stack::HostStack st;
    out.assign(std::size_t(frames), {});
    for (int f = 0; f < frames; ++f) if (!st.run(specs, post_stack::Stage::Scene, in, f, out[std::size_t(f)], err)) return false;
    return true;
}
double maxRel(const HostFrame& g, const HostFrame& c) {
    double m = 0;
    for (std::size_t i = 0; i < c.rgba.size(); ++i) m = std::max(m, std::abs(double(g.rgba[i]) - double(c.rgba[i])) / std::max(1.0, std::abs(double(c.rgba[i]))));
    return m;
}
constexpr double kGpuRel = 1e-5, kDiffering = 1e-4;
}  // namespace

bool PostStackResult::cpuPass() const {
    bool ok = lists > 0 && planMismatch == 0 && planControlDiffers && !chains.empty();
    for (const PostChain& c : chains) ok = ok && c.cpuPass;
    return ok;
}
bool PostStackResult::gpuPass() const {
    bool ok = !chains.empty();
    for (const PostChain& c : chains) ok = ok && c.gpuRun && c.gpuPass;
    return ok;
}

PostStackResult postStackParity(rhi::Device* dev, const std::string& path, std::string& err) {
    PostStackResult r;
    fx::Bundle b;
    if (!fx::loadBundle(path, b, err)) return r;
    int controlDiffers = 0;
    for (const Json& l : b.head.get("lists")->a) {
        const std::vector<Spec> specs = specsOf(*l.get("passes"));
        const bool colour = l.get("colour")->b;
        r.lists++;
        if (!samePlan(post_stack::plan(specs, colour), *l.get("plan"))) r.planMismatch++;
        if (!samePlan(post_stack::plan(specs, !colour), *l.get("plan"))) controlDiffers++;
    }
    r.planControlDiffers = controlDiffers > 0;
    const int W = int(b.head.get("width")->number("width")), H = int(b.head.get("height")->number("height"));
    const int F = int(b.head.get("frames")->number("frames"));
    const HostFrame in{W, H, b.arr<float>("input", "f32")};
    for (const Json& c : b.head.get("chains")->a) {
        PostChain ch; ch.id = c.get("id")->s;
        const post_stack::Plan p = post_stack::plan(specsOf(*c.get("passes")), c.get("colour")->b);
        std::vector<HostFrame> host, bent;
        if (p.refused || !runHost(p.scene, in, F, host, err)) { if (err.empty()) err = "chain " + ch.id + " refused: " + p.why; return r; }
        long differ = 0;
        for (int f = 0; f < F; ++f) {
            const auto js = b.arr<float>(ch.id + ".frame" + std::to_string(f), "f32");
            for (std::size_t i = 0; i < js.size(); ++i) {
                ch.values++;
                if (host[std::size_t(f)].rgba[i] == js[i]) continue;
                differ++;
                ch.cpuMaxUlp = std::max(ch.cpuMaxUlp, ulpsApart(host[std::size_t(f)].rgba[i], js[i]));
            }
        }
        ch.cpuDifferingFraction = double(differ) / double(ch.values);
        // Control: the first two passes swapped (chains of two or more passes).
        std::vector<Spec> swapped = p.scene;
        const bool canSwap = swapped.size() >= 2;
        if (canSwap) {
            std::swap(swapped[0], swapped[1]);
            if (!runHost(swapped, in, 1, bent, err)) return r;
            const auto js0 = b.arr<float>(ch.id + ".frame0", "f32");
            double worst = 0; long d = 0;
            for (std::size_t i = 0; i < js0.size(); ++i) if (bent[0].rgba[i] != js0[i]) { d++; worst = std::max(worst, ulpsApart(bent[0].rgba[i], js0[i])); }
            ch.cpuControlFails = worst > 1 || double(d) / double(js0.size()) > kDiffering;
        }
        ch.cpuPass = ch.values > 0 && ch.cpuMaxUlp <= 1 && ch.cpuDifferingFraction <= kDiffering && (!canSwap || ch.cpuControlFails);
        if (dev) {
            post_gpu::DeviceStack ds(*dev);
            for (int f = 0; f < F; ++f) {
                HostFrame g;
                if (!ds.run(p.scene, post_stack::Stage::Scene, in, f, 0.0, g, err)) return r;
                ch.gpuMaxRel = std::max(ch.gpuMaxRel, maxRel(g, host[std::size_t(f)]));
                if (f == 0 && canSwap) ch.gpuControlFails = maxRel(g, bent[0]) > kGpuRel;
            }
            ch.gpuRun = true;
            ch.gpuPass = ch.gpuMaxRel <= kGpuRel && (!canSwap || ch.gpuControlFails);
        }
        r.chains.push_back(ch);
    }
    return r;
}

}  // namespace raw::gpu_check
