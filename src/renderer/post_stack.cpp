// The post stack's driver: see raw/renderer/post_stack.hpp. Operation order follows
// web/motion/post.mjs (planScenePasses), web/motion/renderer.mjs and web/motion/post_run.mjs.
#include "raw/renderer/post_stack.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::post_stack {

const std::vector<PassInfo>& registry() {
    static const std::vector<PassInfo> r = {
        {"exposure", Stage::Scene, false, Input::Any, Output::Linear},
        {"srgb-decode", Stage::Scene, false, Input::Any, Output::Linear},
        {"crt", Stage::Display, false, Input::Any, Output::Linear},
        {"film", Stage::Scene, false, Input::Linear, Output::Display},
        {"crt-classic", Stage::Display, false, Input::Any, Output::Linear},
        {"dither", Stage::Display, false, Input::Any, Output::Linear},
        {"paint", Stage::Scene, false, Input::Linear, Output::Display},
    };
    return r;
}
const PassInfo* find(const std::string& name) {
    for (const PassInfo& p : registry()) if (p.name == name) return &p;
    return nullptr;
}
double Spec::get(const std::string& key, double otherwise) const {
    for (const auto& [k, v] : num) if (k == key) return v;
    return otherwise;
}

Plan plan(const std::vector<Spec>& specs, bool colourManaged) {
    Plan out;
    auto refuse = [&](std::string why) { out = Plan{}; out.refused = true; out.why = std::move(why); return out; };
    bool linear = colourManaged;   // the encoding of the frame so far: linear light or display values
    std::string prevName;          // the last display-output pass before here
    for (const Spec& spec : specs) {
        const PassInfo* p = find(spec.pass);
        if (!p) return refuse("post pass " + spec.pass + " is not registered");
        if (p->stage == Stage::Display) { out.display.push_back(spec); continue; }
        if (p->input == Input::Linear && !linear) {
            if (!prevName.empty()) return refuse("post pass " + p->name + " reads scene-linear light, but " + prevName + " before it writes display values");
            out.scene.push_back(Spec{"srgb-decode", {}});
            linear = true;
        }
        if (p->output == Output::Display) {
            if (colourManaged) return refuse("post pass " + p->name + " writes display values (it is its own tone mapper), so the list must not set post.colour");
            linear = false; prevName = p->name;
        } else if (p->input == Input::Linear) linear = true;
        out.scene.push_back(spec);
    }
    return out;
}

Schedule schedule(const std::vector<Spec>& specs, Stage stage, bool toOutput) {
    Schedule s;
    int cur = kIn, n = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const PassInfo* p = find(specs[i].pass);
        if (!p) throw std::invalid_argument("post pass " + specs[i].pass + " is not registered");
        if (p->stage != stage) throw std::invalid_argument("post pass " + p->name + " is listed among the other stage's passes");
        const bool last = i + 1 == specs.size();
        if (p->history) {
            const int hist = kHist0 + int(i);
            s.steps.push_back({false, i, cur, hist, kHistNext});
            s.steps.push_back({true, i, kHistNext, kHistNext, hist});
            cur = hist;
            if (last && toOutput) s.steps.push_back({true, i, hist, hist, kOut});
        } else if (last && toOutput) {
            s.steps.push_back({false, i, cur, cur, kOut});
            cur = kOut;
        } else {
            const int dst = (n++ % 2) ? kPing1 : kPing0;
            s.steps.push_back({false, i, cur, cur, dst});
            cur = dst;
        }
    }
    s.result = (toOutput && !specs.empty()) ? kOut : cur;
    return s;
}

double srgbDecode(double v) {
    const double a = std::abs(v), l = a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4);
    return v < 0 ? -l : l;
}
void exposureCpu(const HostFrame& src, HostFrame& dst, double ev) {
    const double k = std::pow(2.0, ev);
    for (std::size_t i = 0; i < src.rgba.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) dst.rgba[i + c] = float(double(src.rgba[i + c]) * k);
        dst.rgba[i + 3] = src.rgba[i + 3];
    }
}
void srgbDecodeCpu(const HostFrame& src, HostFrame& dst) {
    for (std::size_t i = 0; i < src.rgba.size(); ++i) dst.rgba[i] = (i & 3) == 3 ? src.rgba[i] : float(srgbDecode(src.rgba[i]));
}

HostStack::HostStack() {
    define("exposure", [](const HostFrame& s, const HostFrame&, HostFrame& d, const Spec& spec, int) { exposureCpu(s, d, spec.get("ev", 0)); });
    define("srgb-decode", [](const HostFrame& s, const HostFrame&, HostFrame& d, const Spec&, int) { srgbDecodeCpu(s, d); });
}
void HostStack::define(const std::string& name, CpuPass fn) {
    for (auto& [n, f] : passes_) if (n == name) { f = std::move(fn); return; }
    passes_.emplace_back(name, std::move(fn));
}
bool HostStack::has(const std::string& name) const {
    return std::any_of(passes_.begin(), passes_.end(), [&](const auto& p) { return p.first == name; });
}

bool HostStack::run(const std::vector<Spec>& specs, Stage stage, const HostFrame& input, int frame, HostFrame& out, std::string& err) {
    Schedule sc;
    try { sc = schedule(specs, stage, false); } catch (const std::exception& e) { err = e.what(); return false; }
    for (const Spec& s : specs) if (!has(s.pass)) { err = "post pass " + s.pass + " has no C++ reference"; return false; }
    const int nframes = kHist0 + int(specs.size());
    std::vector<HostFrame> f(std::size_t(nframes), HostFrame{input.w, input.h, std::vector<float>(input.rgba.size(), 0.0f)});
    f[kIn] = input;
    history_.resize(specs.size(), HostFrame{input.w, input.h, std::vector<float>(input.rgba.size(), 0.0f)});
    for (std::size_t i = 0; i < specs.size(); ++i) f[std::size_t(kHist0) + i] = history_[i];
    graph::FrameGraph g;
    std::vector<graph::Resource> r;
    for (int k = 0; k < nframes; ++k) r.push_back(g.createHost("post frame " + std::to_string(k)));
    // Frames that hold data before the stage runs (input and history) are written by a first pass.
    std::vector<graph::Use> seed{{r[kIn], rhi::Access::StorageWrite}};
    for (std::size_t i = 0; i < specs.size(); ++i) seed.push_back({r[std::size_t(kHist0) + i], rhi::Access::StorageWrite});
    g.addPass("post seed", seed, [](graph::PassContext&) {});
    for (const Step& st : sc.steps) {
        std::vector<graph::Use> uses{{r[std::size_t(st.src)], rhi::Access::StorageRead}};
        if (st.prev != st.src) uses.push_back({r[std::size_t(st.prev)], rhi::Access::StorageRead});
        uses.push_back({r[std::size_t(st.dst)], rhi::Access::StorageWrite});
        const std::string name = st.copy ? "post copy" : "post " + specs[st.spec].pass;
        const CpuPass* fn = nullptr;
        if (!st.copy) for (const auto& [n, p] : passes_) if (n == specs[st.spec].pass) fn = &p;
        g.addPass(name, uses, [&f, st, fn, &specs, frame](graph::PassContext&) {
            if (st.copy) { f[std::size_t(st.dst)].rgba = f[std::size_t(st.src)].rgba; return; }
            (*fn)(f[std::size_t(st.src)], f[std::size_t(st.prev)], f[std::size_t(st.dst)], specs[st.spec], frame);
        });
    }
    g.markOutput(r[std::size_t(sc.result)]);
    for (std::size_t i = 0; i < specs.size(); ++i) g.markOutput(r[std::size_t(kHist0) + i]);
    if (!g.execute(err)) return false;
    for (std::size_t i = 0; i < specs.size(); ++i) history_[i] = f[std::size_t(kHist0) + i];
    out = f[std::size_t(sc.result)];
    return true;
}

}  // namespace raw::post_stack
