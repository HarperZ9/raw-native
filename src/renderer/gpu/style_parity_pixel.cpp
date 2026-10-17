// Perspective-stable pixel art's checks and its GPU runner (capture and shade of the live probes,
// clear, the two atomic splat passes, resolve; one submission for the last frame of a case, with
// the crossfade state of the C++ frame loop): bounds and controls of
// evidence/cpp-port-style-bounds-10.json, written before the first run.
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/style_pixel.hpp"
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

struct PixelParamsGpu { std::uint32_t width, height, n, hasPrev; float origin[4], a[4], eye[4], f[4], r[4], u[4]; };
static_assert(sizeof(PixelParamsGpu) == 112, "PixelGpuParams layout");
void put(float* o, const style::PV3& v, double w) { o[0] = float(v[0]); o[1] = float(v[1]); o[2] = float(v[2]); o[3] = float(w); }

PixelParamsGpu paramsOf(const style::PixelOptions& o, const style::PixelCamera& cam, const style::PV3& origin, double blend, bool hasPrev) {
    PixelParamsGpu P{std::uint32_t(cam.w), std::uint32_t(cam.h), std::uint32_t(o.N), hasPrev ? 1u : 0u, {}, {}, {}, {}, {}, {}};
    put(P.origin, origin, o.bands);
    P.a[0] = float(o.outline); P.a[1] = float(o.crease); P.a[2] = float(o.expand); P.a[3] = float(blend);
    put(P.eye, cam.eye, cam.ty); put(P.f, cam.f, 0); put(P.r, cam.r, 0); put(P.u, cam.u, 0);
    return P;
}

bool runPixelGpu(rhi::Device& dev, const style::PixelOptions& o, const style::PixelRunner::Frame& F, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const int live = F.hasPrev ? 2 : 1;
    PixelParamsGpu P[2] = {paramsOf(o, F.cam, F.probe->origin, F.blend, F.hasPrev), paramsOf(o, F.cam, F.hasPrev ? F.prev->origin : F.probe->origin, F.blend, F.hasPrev)};
    const std::uint32_t T = std::uint32_t(6 * o.N * o.N), n = std::uint32_t(F.cam.w * F.cam.h);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    rhi::BufferHandle ub[2], geo[2], nrm[2], col[2], zb[2], idb[2];
    for (int s = 0; s < 2; ++s) {
        ub[s] = gpu.make(sizeof(PixelParamsGpu), BufferUsage::Uniform | BufferUsage::CopyDst, "pixel params");
        geo[s] = gpu.make(std::uint64_t(T) * 16, st, "probe geo"); nrm[s] = gpu.make(std::uint64_t(T) * 16, st, "probe normals"); col[s] = gpu.make(std::uint64_t(T) * 16, st, "probe colour");
        zb[s] = gpu.make(std::uint64_t(n) * 4, st, "splat depth"); idb[s] = gpu.make(std::uint64_t(n) * 4, st, "splat ids");
    }
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "pixel packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "pixel readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    graph::Resource U[2], G[2], Nr[2], C[2], Z[2], I[2];
    for (int s = 0; s < 2; ++s) {
        U[s] = g.importBuffer("params", ub[s]); G[s] = g.importBuffer("geo", geo[s]); Nr[s] = g.importBuffer("nrm", nrm[s]); C[s] = g.importBuffer("col", col[s]);
        Z[s] = g.importBuffer("zb", zb[s]); I[s] = g.importBuffer("idb", idb[s]);
    }
    const graph::Resource O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("pixel upload", {{U[0], Access::CopyDst}, {U[1], Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->upload(ub[0], 0, &P[0], sizeof P[0]); c.cmd->upload(ub[1], 0, &P[1], sizeof P[1]);
    });
    style_graph::Passes ps{dev, g, {}};
    for (int s = 0; s < live; ++s) {
        if (!ps.add("style_pixel_capture", {U[s], G[s], Nr[s]}, T, err) || !ps.add("style_pixel_shade", {U[s], G[s], Nr[s], C[s]}, T, err) ||
            !ps.add("style_pixel_clear", {U[s], Z[s]}, n, err) || !ps.add("style_pixel_clear", {U[s], I[s]}, n, err) ||
            !ps.add("style_pixel_splatz", {U[s], G[s], Z[s]}, T, err) || !ps.add("style_pixel_splatid", {U[s], G[s], Z[s], I[s]}, T, err)) return false;
    }
    // The second slot's buffers are bound even with no previous probe (the frame graph takes a
    // resource once a pass); the resolve reads them only when has_prev is set.
    if (!ps.add("style_pixel_resolve", {U[0], C[0], C[1], I[0], I[1], O}, n, err)) return false;
    g.addPass("pixel readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

struct Codes { double over2{0}, mean{0}; int max{0}; };
Codes compare(const std::vector<std::uint8_t>& want, const std::vector<std::uint8_t>& got) {
    Codes s; long n = 0, over = 0; double sum = 0;
    for (std::size_t i = 0; i < want.size(); ++i) { if ((i & 3) == 3) continue; const int d = std::abs(int(want[i]) - int(got[i])); sum += d; n++; over += d > 2; s.max = std::max(s.max, d); }
    s.over2 = double(over) / double(n); s.mean = sum / double(n);
    return s;
}
std::vector<std::uint8_t> codesOf(const std::vector<float>& img) {
    std::vector<std::uint8_t> o(img.size());
    for (std::size_t i = 0; i < img.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::pixelEncode8(img[i]);
    return o;
}
std::vector<std::uint8_t> unpack(const std::vector<std::uint32_t>& w) {
    std::vector<std::uint8_t> o(w.size() * 4);
    std::memcpy(o.data(), w.data(), o.size());
    return o;
}
style::PV3 v3(const Json& a) { return {a.at(0).number("v"), a.at(1).number("v"), a.at(2).number("v")}; }
bool cpuWithin(const Codes& c) { return c.over2 <= 0.002 && c.mean <= 0.05; }
bool gpuWithin(const Codes& c) { return c.over2 <= 0.03 && c.mean <= 2.0; }
}  // namespace

// The pixel cases; returns the number the bundle holds.
int pixelParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/pixel.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "pixel"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("w")), h = int(c.get("height")->number("h"));
        style::PixelOptions o; o.N = int(c.get("N")->number("N"));
        style::PixelOptions ob = o; ob.bands = 5;
        style::PixelRunner run(o, w, h), bent(ob, w, h);
        style::PixelRunner::Frame F, BF;
        for (const Json& v : c.get("views")->a) {
            F = run.frame(v3(*v.get("eye")), v3(*v.get("target")), v.get("fovy")->number("fovy"));
            BF = bent.frame(v3(*v.get("eye")), v3(*v.get("target")), v.get("fovy")->number("fovy"));
        }
        const auto jsDepth = b.arr<float>(s.id + ".depth", "f32");
        const auto jsMat = b.arr<std::uint32_t>(s.id + ".mat", "u32");
        const auto jsObj = b.arr<std::uint32_t>(s.id + ".obj", "u32");
        const auto jsIdb = b.arr<std::uint32_t>(s.id + ".idb", "u32");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        long texDiff = 0, visDiff = 0;
        for (std::size_t k = 0; k < jsDepth.size(); ++k)
            texDiff += std::memcmp(&jsDepth[k], &F.probe->depth[k], 4) != 0 || std::int32_t(jsMat[k]) != F.probe->mat[k] || std::int32_t(jsObj[k]) != F.probe->obj[k];
        for (std::size_t p = 0; p < jsIdb.size(); ++p) visDiff += jsIdb[p] != F.vis.idb[p];
        const double texFrac = double(texDiff) / double(jsDepth.size()), visFrac = double(visDiff) / double(jsIdb.size());
        const bool blendEq = std::abs(F.blend - c.get("blend")->number("blend")) <= 1e-12;
        s.presetEqual = blendEq && texFrac <= 0.001 && visFrac <= 0.001;
        if (!s.presetEqual) s.presetDetail = "blend " + std::to_string(blendEq) + ", probe texels differing " + std::to_string(texDiff) + ", visibility pixels differing " + std::to_string(visDiff);
        s.cpuRgbaMismatch = texDiff + visDiff;
        const std::vector<std::uint8_t> codes = codesOf(F.img), bcodes = codesOf(BF.img);
        const Codes cc = compare(js, codes), cb = compare(js, bcodes);
        s.decisions = long(js.size() / 4 * 3);
        s.cpuMismatch = long(std::lround(cc.over2 * double(s.decisions)));
        s.cpuMaxAbs = cc.mean;
        s.cpuControlFails = !cpuWithin(cb);
        s.cpuPass = s.presetEqual && cpuWithin(cc) && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runPixelGpu(*dev, o, F, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const std::vector<std::uint8_t> g8 = unpack(got);
            const Codes gc = compare(codes, g8), gb = compare(bcodes, g8);
            s.gpuOver2Fraction = gc.over2; s.gpuMean = gc.mean; s.gpuMax = gc.max;
            s.gpuControlFails = !gpuWithin(gb);
            s.gpuPass = gpuWithin(gc) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
