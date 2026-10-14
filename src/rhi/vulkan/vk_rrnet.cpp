// HW H2 inference: our ray reconstruction network on our own Vulkan kernels, the cooperative
// matrix path and the portable scalar path, checked against the PyTorch forward pass and timed
// (raw/rhi/hw_vk.hpp; bounds in evidence/rr-inference-bounds.json).
#include "vk_context.hpp"
#include "raw/rhi/hw_vk.hpp"
#include "raw_vk_spirv.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
namespace raw::rhi::hw {
namespace {
using vk::Buffer;
using vk::Context;
using vk::Pipeline;

struct Layer { uint32_t cout, cin, k; size_t wOff, bOff, hOff; uint32_t kpad, coutPad; };
// The network's layers in the weight file's order (scripts/rr_export.py LAYERS).
constexpr uint32_t kShape[12][3] = {{24, 16, 3}, {24, 24, 3}, {48, 24, 3}, {48, 48, 3}, {64, 48, 3}, {64, 64, 3},
                                    {48, 112, 1}, {48, 48, 3}, {24, 72, 1}, {24, 24, 3}, {27, 40, 1}, {25, 48, 1}};
// Version 4 (evidence/rr-bounds-amendment-6.json): 25 input features, a 54-channel head and a 50-channel quarter head.
constexpr uint32_t kShapeV4[12][3] = {{24, 25, 3}, {24, 24, 3}, {48, 24, 3}, {48, 48, 3}, {64, 48, 3}, {64, 64, 3},
                                      {48, 112, 1}, {48, 48, 3}, {24, 72, 1}, {24, 24, 3}, {54, 49, 1}, {50, 48, 1}};
const VkSpirvBlob* spirv(const char* name){
    for (const VkSpirvBlob& b : kVkSpirv) if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}
uint16_t toHalf(float f){
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0){
        if (e < -10) return (uint16_t)sign;
        m |= 0x800000u;
        const uint32_t shift = (uint32_t)(14 - e), r = m >> shift, rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
        return (uint16_t)(sign | (r + (rem > half || (rem == half && (r & 1u)))));
    }
    if (e >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
    const uint32_t rem = m & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    return (uint16_t)h;
}
std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
bool readFile(const std::string& p, std::vector<float>& v){
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    v.resize((size_t)f.tellg() / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(v.size() * 4));
    return bool(f);
}
uint32_t down2(uint32_t n){ return (n - 1u) / 2u + 1u; }
// Input channels a pixel: 17, or 32 with the split radiance (version 4).
uint32_t xChannels(int version){ return version >= 4 ? 32u : 17u; }
// One frame's input: fKKK_in.f32, with fKKK_split.f32 interleaved after it for version 4.
bool readInput(const std::string& stem, int version, std::vector<float>& x){
    std::vector<float> in, sp;
    if (!readFile(stem + "_in.f32", in)) return false;
    if (version < 4){ x = std::move(in); return true; }
    if (!readFile(stem + "_split.f32", sp) || in.size() / 17 != sp.size() / 15) return false;
    const size_t n = in.size() / 17;
    x.resize(n * 32);
    for (size_t q = 0; q < n; ++q){
        std::memcpy(&x[q * 32], &in[q * 17], 17 * 4);
        std::memcpy(&x[q * 32 + 17], &sp[q * 15], 15 * 4);
    }
    return true;
}

// Every buffer and pipeline of one network instance at W x H.
struct Net {
    Context& cx;
    uint32_t W, H, w1, h1, w2, h2, w3, h3;
    std::vector<Layer> L;
    Buffer wf, wh, X[2], state, md, feat, aux, act[10], head, headq, acc, f, accq, q, fq, out;
    Pipeline conv, convPerturb, glue[6];
    bool coop{false};
    float eps{1e-3f};
    int version{1};
    explicit Net(Context& c) : cx(c) {}
    ~Net(){
        for (Buffer* b : {&wf, &wh, &X[0], &X[1], &state, &md, &feat, &aux, &head, &headq, &acc, &f, &accq, &q, &fq, &out}) cx.destroy(*b);
        for (Buffer& b : act) cx.destroy(b);
        cx.destroy(conv); cx.destroy(convPerturb);
        for (Pipeline& p : glue) cx.destroy(p);
    }
    bool init(const std::vector<float>& weights, uint32_t width, uint32_t height, bool useCoop, std::string& err);
    std::vector<Context::Step> frame(int xi, bool first, bool perturb);
};

bool Net::init(const std::vector<float>& weights, uint32_t width, uint32_t height, bool useCoop, std::string& err){
    W = width; H = height; w1 = down2(W); h1 = down2(H); w2 = down2(w1); h2 = down2(h1); w3 = down2(w2); h3 = down2(h2);
    coop = useCoop;
    size_t off = 0, hoff = 0;
    for (const auto& s : (version >= 4 ? kShapeV4 : kShape)){
        Layer l{s[0], s[1], s[2], off, off + (size_t)s[0] * s[1] * s[2] * s[2], hoff, 0, 0};
        l.kpad = (l.cin * l.k * l.k + 15u) / 16u * 16u; l.coutPad = (l.cout + 15u) / 16u * 16u;
        off = l.bOff + l.cout; hoff += (size_t)l.kpad * l.coutPad;
        L.push_back(l);
    }
    if (off != weights.size()){ err = "weight file has " + std::to_string(weights.size()) + " floats, the network needs " + std::to_string(off); return false; }
    std::vector<uint16_t> half(hoff, 0);
    for (const Layer& l : L)
        for (uint32_t co = 0; co < l.cout; ++co) for (uint32_t c = 0; c < l.cin; ++c) for (uint32_t t = 0; t < l.k * l.k; ++t)
            half[l.hOff + (size_t)(c * l.k * l.k + t) * l.coutPad + co] = toHalf(weights[l.wOff + ((size_t)co * l.cin + c) * l.k * l.k + t]);
    const uint64_t n = (uint64_t)W * H, n1 = (uint64_t)w1 * h1, n2 = (uint64_t)w2 * h2, n3 = (uint64_t)w3 * h3;
    const uint64_t actN[10] = {n1 * 24, n1 * 24, n2 * 48, n2 * 48, n3 * 64, n3 * 64, n2 * 48, n2 * 48, n1 * 24, n1 * 24};
    bool ok = cx.deviceBuffer(weights.size() * 4, wf, err) && cx.upload(wf, weights.data(), weights.size() * 4, err) &&
              cx.deviceBuffer(half.size() * 2 + 64, wh, err) && cx.upload(wh, half.data(), half.size() * 2, err) &&
              cx.deviceBuffer(n * xChannels(version) * 4, X[0], err) && cx.deviceBuffer(n * xChannels(version) * 4, X[1], err) &&
              cx.deviceBuffer(n * (version >= 4 ? 24 : 12), state, err) &&
              cx.deviceBuffer(16, md, err) && cx.deviceBuffer(n * (version >= 4 ? 100 : 64), feat, err) && cx.deviceBuffer(n * (version >= 4 ? 88 : 40), aux, err) &&
              cx.deviceBuffer(n * (version >= 4 ? 54 : 27) * 4, head, err) && cx.deviceBuffer(n2 * (version >= 4 ? 50 : 25) * 4, headq, err) && cx.deviceBuffer(n * 12, acc, err) &&
              cx.deviceBuffer(n * 12, f, err) && cx.deviceBuffer(n2 * 12, accq, err) && cx.deviceBuffer(n2 * 12, q, err) &&
              cx.deviceBuffer(n2 * 12, fq, err) && cx.deviceBuffer(n * 12, out, err);
    for (int i = 0; ok && i < 10; ++i) ok = cx.deviceBuffer(actN[i] * 4, act[i], err);
    if (!ok) return false;
    const VkSpirvBlob* cs = spirv(coop ? "vk_rr_conv_coop" : "vk_rr_conv");
    const VkSpirvBlob* cp = spirv("vk_rr_conv_perturb");
    if (!cs || !cp || !cx.pipeline(cs->words, cs->bytes, 5, 64, conv, err) || !cx.pipeline(cp->words, cp->bytes, 5, 64, convPerturb, err)) return false;
    for (int m = 0; m < 6; ++m){
        const std::string name = "vk_rr_glue" + std::to_string(m);
        const VkSpirvBlob* g = spirv(name.c_str());
        if (!g || !cx.pipeline(g->words, g->bytes, 7, 32, glue[m], err)) return false;
    }
    return true;
}

std::vector<Context::Step> Net::frame(int xi, bool first, bool perturb){
    std::vector<Context::Step> s;
    Buffer& x = X[xi];
    Buffer& xp = X[1 - xi];
    auto g = [&](int m, std::vector<Buffer*> b, uint32_t a, uint32_t bb, uint32_t c, uint32_t d, uint32_t C, uint32_t fl, uint32_t groups){
        while (b.size() < 7) b.push_back(&md);
        uint32_t e; std::memcpy(&e, &eps, 4);
        s.push_back({&glue[m], b, {a, bb, c, d, C, fl, e, (uint32_t)version}, groups, 1});
    };
    const uint32_t n = W * H, n2 = w2 * h2;
    g(0, {&x, &md, &md, &md, &md}, W, H, 0, 0, xChannels(version), 0, 1);
    g(1, {&x, &xp, &state, &md, &feat, &aux}, W, H, 0, 0, 0, first ? 1u : 0u, (n + 255) / 256);
    // conv(layer, src0, s0 size, C0, mode, src1, C1, in size, out size, stride, relu, dst)
    auto conv1 = [&](int li, Buffer* s0, uint32_t s0W, uint32_t s0H, uint32_t C0, uint32_t mode, Buffer* s1, uint32_t C1,
                     uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, uint32_t stride, uint32_t relu, Buffer* d){
        const Layer& l = L[(size_t)li];
        const uint32_t npix = outW * outH;
        const bool pert = perturb && li == 0 && !coop;
        s.push_back({pert ? &convPerturb : &conv, {s0, s1 ? s1 : s0, &wf, &wh, d},
                     {inW, inH, outW, outH, s0W, s0H, C0, mode, C1, l.cout, l.k, stride, relu, (uint32_t)(coop ? l.hOff : l.wOff), (uint32_t)l.bOff, l.coutPad},
                     coop ? (npix + 15) / 16 : (npix + 63) / 64, 1});
    };
    const uint32_t nf = version >= 4 ? 25u : 16u;
    conv1(0, &feat, W, H, nf, 0, nullptr, 0, W, H, w1, h1, 2, 1, &act[0]);
    conv1(1, &act[0], w1, h1, 24, 0, nullptr, 0, w1, h1, w1, h1, 1, 1, &act[1]);
    conv1(2, &act[1], w1, h1, 24, 0, nullptr, 0, w1, h1, w2, h2, 2, 1, &act[2]);
    conv1(3, &act[2], w2, h2, 48, 0, nullptr, 0, w2, h2, w2, h2, 1, 1, &act[3]);
    conv1(4, &act[3], w2, h2, 48, 0, nullptr, 0, w2, h2, w3, h3, 2, 1, &act[4]);
    conv1(5, &act[4], w3, h3, 64, 0, nullptr, 0, w3, h3, w3, h3, 1, 1, &act[5]);
    conv1(6, &act[5], w3, h3, 64, 1, &act[3], 48, w2, h2, w2, h2, 1, 1, &act[6]);
    conv1(7, &act[6], w2, h2, 48, 0, nullptr, 0, w2, h2, w2, h2, 1, 1, &act[7]);
    conv1(8, &act[7], w2, h2, 48, 1, &act[1], 24, w1, h1, w1, h1, 1, 1, &act[8]);
    conv1(9, &act[8], w1, h1, 24, 0, nullptr, 0, w1, h1, w1, h1, 1, 1, &act[9]);
    conv1(10, &act[9], w1, h1, 24, 2, &feat, nf, W, H, W, H, 1, 0, &head);
    conv1(11, &act[7], w2, h2, 48, 0, nullptr, 0, w2, h2, w2, h2, 1, 0, &headq);
    if (version >= 4){   // each signal: blend, full and quarter kernels, pool, combine (amendment 6)
        for (uint32_t i = 0; i < 2; ++i){
            g(2, {&head, &aux, &md, &md, &acc}, W, H, 0, 0, 0, i, (n + 255) / 256);
            g(3, {&acc, &head, &md, &md, &f}, 0, 0, W, H, 54, 25 * i, (n + 255) / 256);
            g(4, {&acc, &md, &md, &md, &accq}, W, H, w2, h2, 0, 0, (n2 + 255) / 256);
            g(3, {&accq, &headq, &md, &md, &q}, 0, 0, w2, h2, 50, 25 * i, (n2 + 255) / 256);
            g(4, {&f, &md, &md, &md, &fq}, W, H, w2, h2, 0, 0, (n2 + 255) / 256);
            g(5, {&f, &fq, &q, &head, &out, &aux, &state}, W, H, w2, h2, 0, i, (n + 255) / 256);
        }
        return s;
    }
    g(2, {&head, &aux, &md, &md, &acc}, W, H, 0, 0, 0, 0, (n + 255) / 256);
    g(3, {&acc, &head, &md, &md, &f}, 0, 0, W, H, 27, 0, (n + 255) / 256);
    g(4, {&acc, &md, &md, &md, &accq}, W, H, w2, h2, 0, 0, (n2 + 255) / 256);
    g(3, {&accq, &headq, &md, &md, &q}, 0, 0, w2, h2, 25, 0, (n2 + 255) / 256);
    g(4, {&f, &md, &md, &md, &fq}, W, H, w2, h2, 0, 0, (n2 + 255) / 256);
    g(5, {&f, &fq, &q, &head, &out, &aux, &state}, W, H, w2, h2, 0, 0, (n + 255) / 256);
    return s;
}

struct Stats { double maxAbs{0}, sumAbs{0}; uint64_t n{0}; int frame{-1}; uint64_t pixel{0}; std::vector<float> top; };
// One path (coop or scalar, optionally the perturbed control) over a sequence, against the reference.
bool parity(Context& cx, const std::vector<float>& weights, const RrArgs& a, bool coop, bool perturb, Stats& st, std::string& err){
    std::vector<float> x;
    char stem[32];
    std::snprintf(stem, sizeof stem, "/f%03d", 0);
    std::ifstream js(a.seq + stem + ".json");
    const std::string meta((std::istreambuf_iterator<char>(js)), std::istreambuf_iterator<char>());
    const auto pw = meta.find("\"width\":"), ph = meta.find("\"height\":");
    if (pw == std::string::npos || ph == std::string::npos){ err = "no frame metadata in " + a.seq; return false; }
    const uint32_t W = (uint32_t)std::atoi(meta.c_str() + pw + 8), H = (uint32_t)std::atoi(meta.c_str() + ph + 9);
    Net net(cx);
    net.eps = a.eps; net.version = a.version;
    if (!net.init(weights, W, H, coop, err)) return false;
    std::vector<float> outv((size_t)W * H * 3), ref;
    for (int k = 0; k < a.frames; ++k){
        std::snprintf(stem, sizeof stem, "/f%03d", k);
        if (!readInput(a.seq + stem, a.version, x) || !readFile(a.ref + stem + ".f32", ref)){ if (k == 0){ err = "no frames"; return false; } break; }
        const int xi = k & 1;
        if (!cx.upload(net.X[xi], x.data(), x.size() * 4, err) || !cx.runBatch(net.frame(xi, k == 0, perturb), nullptr, err) ||
            !cx.download(net.out, outv.data(), outv.size() * 4, err)) return false;
        if (const char* dump = std::getenv("RAW_NATIVE_RR_DUMP")){   // diagnosis: aux and head of every frame
            std::vector<float> aux((size_t)W * H * (a.version >= 4 ? 22 : 10)), head((size_t)W * H * (a.version >= 4 ? 54 : 27));
            cx.download(net.aux, aux.data(), aux.size() * 4, err); cx.download(net.head, head.data(), head.size() * 4, err);
            std::ofstream(std::string(dump) + stem + "_aux.f32", std::ios::binary).write(reinterpret_cast<const char*>(aux.data()), (std::streamsize)(aux.size() * 4));
            std::ofstream(std::string(dump) + stem + "_head.f32", std::ios::binary).write(reinterpret_cast<const char*>(head.data()), (std::streamsize)(head.size() * 4));
        }
        for (size_t i = 0; i < outv.size(); ++i){
            const double d = std::fabs((double)outv[i] - (double)ref[i]);
            if (d > st.maxAbs || std::isnan(d)){ st.maxAbs = std::isnan(d) ? 1e30 : d; st.frame = k; st.pixel = i / 3; }
            st.sumAbs += d; ++st.n;
            if (d > 1e-3) st.top.push_back((float)d);
        }
    }
    return true;
}
std::string statsJson(const Stats& s){
    std::vector<float> t = s.top;
    std::sort(t.begin(), t.end());
    const uint64_t k = (uint64_t)std::ceil(1e-4 * (double)s.n);   // the 99.99th percentile, from the top
    const double p9999 = k <= t.size() && k > 0 ? t[t.size() - k] : 1e-3;
    return "{\"max_abs\":" + num(s.maxAbs) + ",\"p99_99_abs_upper\":" + num(p9999) + ",\"mean_abs\":" + num(s.n ? s.sumAbs / (double)s.n : 0) +
           ",\"values\":" + std::to_string(s.n) + ",\"max_at_frame\":" + std::to_string(s.frame) + ",\"max_at_pixel\":" + std::to_string(s.pixel) +
           ",\"within_1e-2\":" + (s.maxAbs <= 1e-2 ? "true" : "false") + "}";
}
double pct(std::vector<double> v, double p){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double k = p * (double)(v.size() - 1); const size_t i = (size_t)k;
    return i + 1 < v.size() ? v[i] + (k - (double)i) * (v[i + 1] - v[i]) : v[i];
}
// Frame time at 1920 x 1080: the first input frame tiled 4 x 4, one submission a frame.
std::string timing(Context& cx, const std::vector<float>& weights, const RrArgs& a, bool coop, std::string& err){
    std::vector<float> x0;
    if (!readInput(a.seq + "/f000", a.version, x0)){ err = "no input frame for timing"; return ""; }
    const uint32_t sw = 480, sh = 270, W = 1920, H = 1080, xc = xChannels(a.version);
    if (x0.size() != (size_t)sw * sh * xc){ err = "timing expects a 480 x 270 input"; return ""; }
    std::vector<float> x((size_t)W * H * xc);
    for (uint32_t y = 0; y < H; ++y) for (uint32_t xx = 0; xx < W; ++xx)
        std::memcpy(&x[((size_t)y * W + xx) * xc], &x0[((size_t)(y % sh) * sw + xx % sw) * xc], xc * 4);
    Net net(cx);
    net.eps = a.eps; net.version = a.version;
    if (!net.init(weights, W, H, coop, err) || !cx.upload(net.X[0], x.data(), x.size() * 4, err) || !cx.upload(net.X[1], x.data(), x.size() * 4, err)) return "";
    std::vector<double> ms;
    for (int i = 0; i < a.warmup + a.repeats; ++i){
        double t = 0;
        if (!cx.runBatch(net.frame(i & 1, i == 0, false), &t, err)) return "";
        if (i >= a.warmup) ms.push_back(t);
    }
    return "{\"width\":1920,\"height\":1080,\"n\":" + std::to_string(ms.size()) + ",\"median_ms\":" + num(pct(ms, 0.5)) + ",\"p5_ms\":" +
           num(pct(ms, 0.05)) + ",\"p95_ms\":" + num(pct(ms, 0.95)) + ",\"p99_ms\":" + num(pct(ms, 0.99)) + ",\"iqr_ms\":" +
           num(pct(ms, 0.75) - pct(ms, 0.25)) + ",\"within_2ms\":" + (pct(ms, 0.5) <= 2.0 ? "true" : "false") + "}";
}
}  // namespace

VkRun vulkanRrNet(const RrArgs& a){
    VkRun run;
    Context cx;
    std::string err;
    std::vector<float> weights;
    if (!readFile(a.weights, weights)){ run.json = "{\"error\": \"cannot read the weight file\"}\n"; run.code = 2; return run; }
    if (!cx.init(err)){ run.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; run.code = 4; return run; }
    const bool coopOk = cx.probe().on(kCoopMatrix);
    std::string body;
    bool pass = true;
    for (int path = 0; path < 2; ++path){
        const bool coop = path == 0;
        if (coop && !coopOk){ body += "\"coop\":{\"ran\":false,\"reason\":\"cooperative matrix not offered or disabled\"},"; continue; }
        Stats st;
        if (!parity(cx, weights, a, coop, false, st, err)){ run.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; run.code = 2; return run; }
        pass = pass && st.maxAbs <= 1e-2;
        body += std::string("\"") + (coop ? "coop" : "scalar") + "\":{\"ran\":true,\"parity\":" + statsJson(st);
        if (a.repeats > 0){
            const std::string t = timing(cx, weights, a, coop, err);
            if (t.empty()){ run.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; run.code = 2; return run; }
            body += ",\"timing_1080p\":" + t;
        }
        body += "},";
    }
    Stats ctl;
    if (!parity(cx, weights, a, false, true, ctl, err)){ run.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; run.code = 2; return run; }
    const bool ctlFails = ctl.maxAbs > 1e-2;
    pass = pass && ctlFails;
    run.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/rr-inference-bounds.json\",\n \"device\": \"" +
               jsonEscape(cx.probe().adapter) + "\",\n \"sequence\": \"" + jsonEscape(std::filesystem::path(a.seq).filename().string()) +
               "\",\n \"frames\": " + std::to_string(a.frames) + ",\n " + body + "\n \"control_perturbed\":{\"parity\":" + statsJson(ctl) +
               ",\"fails\":" + (ctlFails ? "true" : "false") + "},\n \"validation_errors\": " + std::to_string(cx.validationErrors()) +
               ",\n \"pass\": " + (pass && cx.validationErrors() == 0 ? "true" : "false") + "\n}\n";
    run.code = pass && cx.validationErrors() == 0 ? 0 : 1;
    return run;
}
}  // namespace raw::rhi::hw
