// The spectral film shader's checks and its GPU runner (expose, two MTF passes, halo down and
// convolution, develop, print; one submission): bounds and controls of
// evidence/cpp-port-style-bounds-6.json, written before the first run.
#include "raw/renderer/style_film.hpp"
#include "raw/renderer/style_parity.hpp"
#include "gpu_util.hpp"
#include "style_fixture.hpp"
#include "style_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
using assets::Json;
using rhi::Access;
using rhi::BufferUsage;
namespace fx = style_fx;

struct FilmParamsGpu {
    std::uint32_t outW, outH, inW, inH, gw, gh, haloQ, haloR, frame, mtfR, mtfDir, pad0;
    float v[19][4];   // a, e0, e1, e2, grain_r, grain_cs, halo_s, neg_dmin, neg_span, neg_k, neg_x0, b, prt_dmin, prt_span, prt_k, log_gain, out0, out1, out2
};
static_assert(sizeof(FilmParamsGpu) == 352, "FilmParams layout");
void put(float* o, double a, double b, double c, double d = 0) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
void put3(float* o, const style::T3& t) { put(o, t[0], t[1], t[2]); }

FilmParamsGpu paramsOf(const style::FilmPlan& plan, int f) {
    const style::FilmStock& t = plan.t;
    FilmParamsGpu P{};
    P.outW = std::uint32_t(plan.outW); P.outH = std::uint32_t(plan.outH); P.inW = std::uint32_t(plan.inW); P.inH = std::uint32_t(plan.inH);
    P.gw = std::uint32_t((plan.outW + plan.haloQ - 1) / plan.haloQ); P.gh = std::uint32_t((plan.outH + plan.haloQ - 1) / plan.haloQ);
    P.haloQ = std::uint32_t(plan.haloQ); P.haloR = std::uint32_t(plan.haloR); P.frame = std::uint32_t(f); P.mtfR = std::uint32_t((plan.mtf.size() / 3) / 2);
    const auto w = style::filmWeave(plan, f);
    put(P.v[0], std::pow(2, plan.p.ev), plan.umPerPx, w[0], w[1]);
    for (int r = 0; r < 3; ++r) put(P.v[1 + r], t.E[std::size_t(3 * r)], t.E[std::size_t(3 * r + 1)], t.E[std::size_t(3 * r + 2)]);
    put3(P.v[4], plan.grainUm);
    put(P.v[5], plan.grainContrast[0] * plan.p.grainAmount, plan.grainContrast[1] * plan.p.grainAmount, plan.grainContrast[2] * plan.p.grainAmount);
    put3(P.v[6], plan.haloStrength); put3(P.v[7], t.negDmin); put3(P.v[8], t.negSpan); put3(P.v[9], t.negK); put3(P.v[10], t.negX0);
    put(P.v[11], t.interimage, t.bleach, t.printBase);
    put3(P.v[12], t.prtDmin); put3(P.v[13], t.prtSpan); put3(P.v[14], t.prtK); put3(P.v[15], t.logGain);
    for (int r = 0; r < 3; ++r) put(P.v[16 + r], t.out[std::size_t(3 * r)], t.out[std::size_t(3 * r + 1)], t.out[std::size_t(3 * r + 2)]);
    return P;
}
std::vector<float> tablesOf(const style::FilmPlan& plan) {
    std::vector<float> a(566, 0.0f);
    const style::FilmStock& t = plan.t;
    for (std::size_t w = 0; w < 41; ++w) {
        for (std::size_t c = 0; c < 3; ++c) {
            a[w * 3 + c] = float(t.negPrim[w][c]); a[164 + w * 3 + c] = float(t.printW[w][c]);
            a[287 + w * 3 + c] = float(t.printPrim[w][c]); a[410 + w * 3 + c] = float(t.projV[w][c]);
        }
        a[123 + w] = float(t.negConst[w]);
    }
    for (std::size_t i = 0; i < 33; ++i) a[533 + i] = plan.jTable[i];
    return a;
}

bool runFilm(rhi::Device& dev, const style::FilmPlan& plan, const std::vector<float>& scene, int f, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    FilmParamsGpu P = paramsOf(plan, f), Px = P, Py = P;
    Px.mtfDir = 0; Py.mtfDir = 1;
    const std::vector<float> tab = tablesOf(plan);
    const auto& pois = style::filmPoissonTable();
    const std::uint32_t n = std::uint32_t(plan.outW * plan.outH), m = P.gw * P.gh, ni = std::uint32_t(plan.inW * plan.inH);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, un = BufferUsage::Uniform | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, un, "film params"), uxb = gpu.make(sizeof P, un, "film params x"), uyb = gpu.make(sizeof P, un, "film params y");
    const rhi::BufferHandle tb = gpu.make(tab.size() * 4, st, "film tables"), pb = gpu.make(pois.size() * 4, st, "film poisson");
    const rhi::BufferHandle kb = gpu.make(plan.haloWeights.size() * 4, st, "film halo kernel"), mb = gpu.make(plan.mtf.size() * 4, st, "film mtf");
    const rhi::BufferHandle sb = gpu.make(std::uint64_t(ni) * 16, st, "film scene"), hb = gpu.make(std::uint64_t(n) * 16, st, "film exposure");
    const rhi::BufferHandle h2 = gpu.make(std::uint64_t(n) * 16, st, "film exposure blur"), db = gpu.make(std::uint64_t(m) * 16, st, "film halo down");
    const rhi::BufferHandle cb = gpu.make(std::uint64_t(m) * 16, st, "film halo"), fb = gpu.make(std::uint64_t(n) * 16, st, "film developed");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "film packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "film readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), UX = g.importBuffer("params x", uxb), UY = g.importBuffer("params y", uyb);
    const graph::Resource T = g.importBuffer("tables", tb), PO = g.importBuffer("poisson", pb), K = g.importBuffer("kernel", kb), MW = g.importBuffer("mtf", mb);
    const graph::Resource S = g.importBuffer("scene", sb), H = g.importBuffer("H", hb), H2 = g.importBuffer("H2", h2), HD = g.importBuffer("hd", db);
    const graph::Resource HC = g.importBuffer("hc", cb), F = g.importBuffer("F", fb), O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("film upload", {{U, Access::CopyDst}, {UX, Access::CopyDst}, {UY, Access::CopyDst}, {T, Access::CopyDst}, {PO, Access::CopyDst}, {K, Access::CopyDst}, {MW, Access::CopyDst}, {S, Access::CopyDst}},
              [&](graph::PassContext& c) {
                  c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(uxb, 0, &Px, sizeof P); c.cmd->upload(uyb, 0, &Py, sizeof P);
                  c.cmd->upload(tb, 0, tab.data(), tab.size() * 4); c.cmd->upload(pb, 0, pois.data(), pois.size() * 4);
                  c.cmd->upload(kb, 0, plan.haloWeights.data(), plan.haloWeights.size() * 4); c.cmd->upload(mb, 0, plan.mtf.data(), plan.mtf.size() * 4);
                  c.cmd->upload(sb, 0, scene.data(), scene.size() * 4);
              });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_film_expose", {U, S, H}, n, err)) return false;
    if (plan.mtf.size() > 3 && (!ps.add("style_film_mtf", {UX, MW, H, H2}, n, err) || !ps.add("style_film_mtf", {UY, MW, H2, H}, n, err))) return false;
    if (!ps.add("style_film_halo_down", {U, H, HD}, m, err) || !ps.add("style_film_halo_conv", {U, K, HD, HC}, m, err) ||
        !ps.add("style_film_develop", {U, H, HC, F}, n, err) || !ps.add("style_film_print", {U, T, PO, F, O}, n, err)) return false;
    g.addPass("film readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

struct Stats { int max{0}, p999{0}; double mean{0}; };
Stats stats(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got) {
    long hist[256] = {}, n = 0; double sum = 0; Stats s;
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) { const int d = std::abs(int(want[i * 4 + k]) - int((got[i] >> (8 * k)) & 255u)); hist[d]++; sum += d; n++; s.max = std::max(s.max, d); }
    s.mean = sum / double(n);
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(n) * 0.999) { s.p999 = d; break; } }
    return s;
}
bool withinGpu(const Stats& s) { return s.max <= 6 && s.p999 <= 1 && s.mean <= 0.05; }
std::vector<std::uint8_t> codesOf(const std::vector<float>& img) {
    std::vector<std::uint8_t> o(img.size());
    for (std::size_t i = 0; i < img.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::filmEncode8(img[i]);
    return o;
}
double edge8(double v) {
    const double l = std::min(1.0, std::max(0.0, v)), q = (l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255 + 0.5, fr = q - std::floor(q);
    return std::min(fr, 1 - fr);
}
bool rel(double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(std::abs(b), 1e-300); }
style::T3 t3(const Json& a) { return {a.at(0).number("t"), a.at(1).number("t"), a.at(2).number("t")}; }
}  // namespace

// The film cases; returns the number the bundle holds.
int filmParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/film.rnfx", b, r.error)) return 0;
    const auto src = b.arr<float>("src", "f32");
    const int sw = int(b.head.get("src")->get("width")->number("w")), sh = int(b.head.get("src")->get("height")->number("h"));
    const auto jsPois = b.arr<std::uint32_t>("poisson", "u32");
    const bool poisEq = jsPois == style::filmPoissonTable();
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "film"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height")), f = int(c.get("frames")->number("frames")) - 1;
        style::FilmParams p = style::filmPreset(c.get("preset")->s);
        const Json& ov = *c.get("overrides");
        if (const Json* v = ov.get("ev")) p.ev = v->number("ev");
        if (const Json* v = ov.get("printerPoints")) p.printerPoints = t3(*v);
        if (const Json* v = ov.get("gateWidthMm")) p.gateWidthMm = v->number("gate");
        if (const Json* v = ov.get("grainAmount")) p.grainAmount = v->number("grain");
        const style::FilmPlan plan = style::resolveFilm(p, sw, sh, w, h);
        const style::FilmStock& t = plan.t;
        const Json& q = *c.get("plan");
        std::string detail;
        auto chk = [&](bool ok, const char* what) { if (!ok) detail += std::string(what) + " "; };
        auto vecEq = [&](const Json& a, const double* v, int n, double tol) { bool ok = int(a.a.size()) == n; for (int i = 0; ok && i < n; ++i) ok = rel(v[i], a.at(std::size_t(i)).number("v"), tol); return ok; };
        chk(vecEq(*q.get("E"), t.E.data(), 9, 1e-12), "E");
        chk(vecEq(*q.get("out"), t.out.data(), 9, 1e-12), "out");
        chk(vecEq(*q.get("logGain"), t.logGain.data(), 3, 1e-10), "logGain");
        chk(vecEq(*q.get("neg")->get("k"), t.negK.data(), 3, 1e-12) && vecEq(*q.get("neg")->get("x0"), t.negX0.data(), 3, 1e-12) && vecEq(*q.get("prt")->get("k"), t.prtK.data(), 3, 1e-12), "curves");
        chk(rel(t.bleach, q.get("bleach")->number("b"), 1e-12) || t.bleach == q.get("bleach")->number("b"), "bleach");
        chk(rel(plan.umPerPx, q.get("umPerPx")->number("u"), 1e-12) && plan.haloQ == int(q.get("haloQ")->number("q")) && plan.haloR == int(q.get("haloR")->number("R")), "umPerPx/halo grid");
        chk(vecEq(*q.get("haloStrength"), plan.haloStrength.data(), 3, 1e-12) || (plan.haloStrength[2] == 0 && vecEq(*q.get("haloStrength"), plan.haloStrength.data(), 2, 1e-12)), "haloStrength");
        chk(vecEq(*q.get("grainUm"), plan.grainUm.data(), 3, 1e-12) && vecEq(*q.get("grainContrast"), plan.grainContrast.data(), 3, 1e-12), "grain");
        auto tabEq = [&](const char* name, const std::vector<style::T3>& v) { const auto js = b.arr<double>(s.id + "." + name, "f64"); bool ok = js.size() == v.size() * 3; for (std::size_t i = 0; ok && i < js.size(); ++i) ok = rel(v[i / 3][i % 3], js[i], 1e-12); return ok; };
        chk(tabEq("negPrim", t.negPrim) && tabEq("printW", t.printW) && tabEq("printPrim", t.printPrim) && tabEq("projV", t.projV), "spectral tables");
        { const auto js = b.arr<double>(s.id + ".negConst", "f64"); bool ok = js.size() == t.negConst.size(); for (std::size_t i = 0; ok && i < js.size(); ++i) ok = rel(t.negConst[i], js[i], 1e-12); chk(ok, "negConst"); }
        auto f32Eq = [&](const char* name, const std::vector<float>& v) { const auto js = b.arr<float>(s.id + "." + name, "f32"); bool ok = js.size() == v.size(); for (std::size_t i = 0; ok && i < js.size(); ++i) ok = js[i] == v[i] || rel(v[i], js[i], 1e-6); return ok; };
        chk(f32Eq("haloWeights", plan.haloWeights), "haloWeights");
        chk(f32Eq("mtf", plan.mtf), "mtf");
        chk(f32Eq("jTable", plan.jTable), "jTable");
        chk(poisEq, "poisson");
        s.presetEqual = detail.empty(); s.presetDetail = detail;
        const auto jsImg = b.arr<float>(s.id + ".img", "f32");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        style::FilmParams pb = p; pb.ev += 1.0 / 3;
        const style::FilmPlan bentPlan = style::resolveFilm(pb, sw, sh, w, h);
        const std::vector<float> img = style::filmFrame(plan, src, f), bent = style::filmFrame(bentPlan, src, f);
        double mx = 0, bmx = 0;
        for (std::size_t i = 0; i < jsImg.size(); ++i) { if ((i & 3) == 3) continue; mx = std::max(mx, std::abs(double(img[i]) - jsImg[i])); bmx = std::max(bmx, std::abs(double(bent[i]) - jsImg[i])); }
        s.cpuMaxAbs = mx;
        const std::vector<std::uint8_t> codes = codesOf(img), bentCodes = codesOf(bent);
        s.decisions = long(js.size() / 4 * 3);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || codes[i] == js[i]) continue;
            (edge8(jsImg[i]) <= 255e-5 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = bmx > 1e-5;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-5 && s.cpuMismatch == 0 && double(s.cpuExcused) <= 1e-4 * double(s.decisions) && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runFilm(*dev, plan, src, f, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(codes, got), sc = stats(bentCodes, got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(sc);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
