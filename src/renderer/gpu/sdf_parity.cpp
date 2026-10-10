// The ray marcher's GPU checks, M2 to M7 of evidence/rt-r3-bounds.json.
#include "raw/renderer/sdf_gpu.hpp"
#include "json_text.hpp"
#include "raw/renderer/sdf_scenes.hpp"
#include "raw/math/primitives.hpp"
#include "raw/renderer/swr_parity.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
namespace raw::gpu_check {
namespace {
using sdf::D3;
int hw() { return int(std::max(1u, std::thread::hardware_concurrency())); }
int W = 256, H = 256;   // M2 to M5 at 128 x 128 with --quick (SwiftShader in a browser: the CPU references run single-threaded)
struct Ctx { rhi::Device& dev; SdfParity& P; bool all{true}; };
void record(Ctx& c, const std::string& s, bool pass) {
    c.P.results += (c.P.results.empty() ? "  " : ",\n  ") + s;
    c.all = c.all && pass;
}
std::vector<sdf::MarchHit> cpuMarch(const sdf::Scene& s) {
    std::vector<sdf::MarchHit> out(std::size_t(W) * H);
    parallelRows(H, hw(), [&](int y) {
        for (int x = 0; x < W; ++x) out[std::size_t(y) * W + std::size_t(x)] = sdf::march(s.prog, sdf::d3(s.eye), sdf::cameraRay(s, x, y, W, H), s.tMax);
    });
    return out;
}
struct M2 { long agree{0}, disagree{0}, grazing{0}, both{0}, tOk{0}, matOk{0}, nOk{0}; bool ok{false}; };
M2 compareMarch(const std::vector<sdf::MarchHit>& c, const std::vector<float>& g) {
    M2 m;
    for (std::size_t p = 0; p < c.size(); ++p) {
        const bool gh = g[p * 8] >= 0.0f;
        if (gh != c[p].hit) { ++m.disagree; m.grazing += c[p].closest <= 1e-3; continue; }
        ++m.agree;
        if (!gh) continue;
        ++m.both;
        m.tOk += std::fabs(double(g[p * 8]) - c[p].t) <= 1e-3 * (1.0 + c[p].t);
        m.matOk += int(g[p * 8 + 1]) == c[p].mat;
        m.nOk += double(g[p * 8 + 4]) * c[p].n.x + double(g[p * 8 + 5]) * c[p].n.y + double(g[p * 8 + 6]) * c[p].n.z >= 0.999;
    }
    const double n = double(c.size()), b = double(std::max(1L, m.both));
    m.ok = double(m.agree) >= 0.998 * n && m.disagree == m.grazing && m.both > 1000 &&
           double(m.tOk) >= 0.995 * b && double(m.matOk) >= 0.995 * b && double(m.nOk) >= 0.99 * b;
    return m;
}
std::string m2Json(const char* scene, const char* what, const M2& m) {
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"M2\", \"scene\": \"%s\", \"case\": \"%s\", \"pixels\": %d, \"hit_miss_agree\": %ld, \"disagree\": %ld, \"grazing\": %ld, "
                  "\"both_hit\": %ld, \"t_within\": %ld, \"material_same\": %ld, \"normal_within\": %ld, \"within\": %s}",
                  scene, what, W * H, m.agree, m.disagree, m.grazing, m.both, m.tOk, m.matOk, m.nOk, m.ok ? "true" : "false");
    return b;
}
// M3: soft shadow and AO at the CPU hit points (rounded to float, as the GPU receives them).
struct M3 { double meanS{0}, p99S{0}, meanA{0}, p99A{0}; long n{0}; bool ok{false}; };
M3 shadingTerms(Ctx& c, const sdf::Scene& s, const std::vector<sdf::MarchHit>& hits, float k) {
    std::vector<float> pts;
    std::vector<std::size_t> at;
    for (std::size_t p = 0; p < hits.size(); ++p) {
        if (!hits[p].hit) continue;
        const D3 d = sdf::cameraRay(s, int(p % W), int(p / W), W, H);
        pts.insert(pts.end(), {float(s.eye.x + d.x * hits[p].t), float(s.eye.y + d.y * hits[p].t), float(s.eye.z + d.z * hits[p].t),
                               float(hits[p].n.x), float(hits[p].n.y), float(hits[p].n.z)});
        at.push_back(p);
    }
    SdfJob j; j.w = W; j.h = H; j.points = pts; j.softK = k;
    const SdfGpu g = sdfGpu(c.dev, s, j);
    M3 m;
    if (!g.error.empty()) { c.P.error = g.error; return m; }
    m.n = long(at.size());
    std::vector<double> ds(at.size()), da(at.size());
    const D3 l = sdf::d3(normalize(s.sunDir));
    parallelRows(int(at.size()), hw(), [&](int i) {
        const float* q = &pts[std::size_t(i) * 6];
        const D3 x{q[0], q[1], q[2]}, n{q[3], q[4], q[5]};
        ds[std::size_t(i)] = std::fabs(sdf::softShadow(s.prog, x, l) - g.terms[std::size_t(i) * 2]);
        da[std::size_t(i)] = std::fabs(sdf::ambientOcclusion(s.prog, x, n) - g.terms[std::size_t(i) * 2 + 1]);
    });
    const auto stats = [](std::vector<double> v, double& mean, double& p99) {
        if (v.empty()) return;
        double s = 0; for (double x : v) s += x;
        mean = s / double(v.size());
        std::sort(v.begin(), v.end());
        p99 = v[std::min(v.size() - 1, std::size_t(0.99 * double(v.size())))];
    };
    stats(ds, m.meanS, m.p99S); stats(da, m.meanA, m.p99A);
    m.ok = m.n > 1000 && m.meanS <= 2e-3 && m.p99S <= 2e-2 && m.meanA <= 2e-3 && m.p99A <= 2e-2;
    return m;
}
std::string m3Json(const char* scene, const char* what, const M3& m) {
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"M3\", \"scene\": \"%s\", \"case\": \"%s\", \"points\": %ld, \"shadow_mean\": %.3e, \"shadow_p99\": %.3e, "
                  "\"ao_mean\": %.3e, \"ao_p99\": %.3e, \"within\": %s}", scene, what, m.n, m.meanS, m.p99S, m.meanA, m.p99A, m.ok ? "true" : "false");
    return b;
}
}  // namespace

namespace {
// M4: 4,096 seeded cases against float64 closed forms.
std::string fog(Ctx& c, bool& ok) {
    sdf::Scene s = sdf::arcade();
    std::vector<float> cases;
    std::uint64_t r = 0xf09ULL;
    const auto next = [&r] { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return double(r >> 11) * (1.0 / 9007199254740992.0); };
    for (int i = 0; i < 4096; ++i) {
        const Vec3 d = normalize(Vec3{float(next() - 0.5), float(next() - 0.5), float(next() - 0.5)});
        cases.insert(cases.end(), {float(next() * 10 - 5), float(next() * 4), float(next() * 10 - 5), d.x, d.y, d.z, float(0.5 + next() * 30.0), 0.0f});
    }
    SdfJob j; j.fogCases = cases;
    const SdfGpu g = sdfGpu(c.dev, s, j);
    if (!g.error.empty()) { c.P.error = g.error; ok = false; return ""; }
    double wh = 0, wf = 0, wm = 0, wc = 0;
    int excluded = 0;
    for (int i = 0; i < 4096; ++i) {
        const float* q = &cases[std::size_t(i) * 8];
        const D3 o{q[0], q[1], q[2]}, d{q[3], q[4], q[5]};
        const double t = q[6], h = sdf::fogHomogeneous(s.fogA, t), f = sdf::fogHeight(s.fogA, s.fogB, o, d, t);
        if (f < 1e-30) { ++excluded; continue; }   // method note 4: below float32's range
        const float* o4 = &g.fog[std::size_t(i) * 4];
        wh = std::max(wh, std::fabs(o4[0] - h) / h); wf = std::max(wf, std::fabs(o4[1] - f) / f);
        wm = std::max(wm, std::fabs(o4[2] - f) / f); wc = std::max(wc, std::fabs(o4[3] - f) / f);
    }
    ok = wh <= 1e-5 && wf <= 1e-4 && wm <= 1e-3 && wc > 1e-3;
    char b[300];
    std::snprintf(b, sizeof b, "{\"check\": \"M4\", \"cases\": 4096, \"excluded_below_1e-30\": %d, \"homogeneous_worst_rel\": %.3e, \"height_closed_worst_rel\": %.3e, "
                  "\"height_march_256_worst_rel\": %.3e, \"control_march_4_worst_rel\": %.3e, \"within\": %s}", excluded, wh, wf, wm, wc, ok ? "true" : "false");
    return b;
}
// M5: god rays against the CPU march with the same sample positions.
std::string godRays(Ctx& c, const sdf::Scene& s, std::uint32_t flags, const char* what, bool& ok) {
    SdfJob j; j.w = W; j.h = H; j.god = true; j.flags = flags;
    const SdfGpu g = sdfGpu(c.dev, s, j);
    if (!g.error.empty()) { c.P.error = g.error; ok = false; return ""; }
    std::vector<double> ref(std::size_t(W) * H);
    std::vector<int> clear(ref.size());
    parallelRows(H, hw(), [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t p = std::size_t(y) * W + std::size_t(x);
            const D3 d = sdf::cameraRay(s, x, y, W, H);
            ref[p] = sdf::godRays(s, sdf::d3(s.eye), d, g.god[p * 4 + 2], clear[p]);
        }
    });
    const double mx = *std::max_element(ref.begin(), ref.end());
    long within = 0, explained = 0, big = 0;
    for (std::size_t p = 0; p < ref.size(); ++p) {
        const double e = std::fabs(g.god[p * 4] - ref[p]) / mx;
        big += e > 0.05;
        if (e <= 1e-3) ++within; else explained += int(g.god[p * 4 + 1]) != clear[p];
    }
    const long n = long(ref.size());
    ok = flags ? double(big) >= 0.01 * double(n) : (double(within) >= 0.995 * double(n) && within + explained == n);
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"M5\", \"scene\": \"%s\", \"case\": \"%s\", \"pixels\": %ld, \"max_inscatter\": %.4e, \"within_1e-3\": %ld, "
                  "\"explained_by_shadow_samples\": %ld, \"over_5_percent\": %ld, \"%s\": %s}",
                  s.name.c_str(), what, n, mx, within, explained, big, flags ? "control_fails" : "within", ok ? "true" : "false");
    return b;
}
}  // namespace

namespace {
// M6: the R1 rasterizer's retro_room and the SDF sculptures through shared depth, against a CPU
// reference that casts every pixel-centre ray at both and keeps the nearer.
struct M6 { long agree{0}, tie{0}, edge{0}, unexplained{0}; };
M6 compareComposite(const sdf::Scene& sc, const swr::Scene& room, const swr::Setup& st, const std::vector<std::uint32_t>& slot,
                    const std::vector<float>& comp, const std::vector<sdf::MarchHit>& sh) {
    const std::vector<Tri> tris = [&] {
        std::vector<Tri> t(room.geo.triangles());
        for (std::size_t i = 0; i < t.size(); ++i) t[i] = {room.geo.pos[room.geo.idx[i * 3]], room.geo.pos[room.geo.idx[i * 3 + 1]], room.geo.pos[room.geo.idx[i * 3 + 2]]};
        return t;
    }();
    std::vector<int> winner(std::size_t(W) * H), tie(winner.size());
    parallelRows(H, hw(), [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t p = std::size_t(y) * W + std::size_t(x);
            const D3 dd = sdf::cameraRay(sc, x, y, W, H);
            const Ray r{sc.eye, Vec3{float(dd.x), float(dd.y), float(dd.z)}};
            float best = 1e30f;
            for (const Tri& t : tris) { float tt, u, v; if (intersectTri(r, t, tt, u, v) && tt < best) best = tt; }
            const bool tri = best < 1e30f, s = sh[p].hit;
            winner[p] = !tri && !s ? -1 : (!s ? 0 : (!tri ? 1 : (sh[p].t < best ? 1 : 0)));
            tie[p] = tri && s && std::fabs(sh[p].t - best) <= 1e-3 * std::min<double>(best, sh[p].t);
        }
    });
    M6 m;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t p = std::size_t(y) * W + std::size_t(x);
            if (int(comp[p * 4]) == winner[p]) { ++m.agree; continue; }
            if (tie[p]) { ++m.tie; continue; }
            bool silhouette = false;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                if (nx >= 0 && ny >= 0 && nx < W && ny < H && sh[std::size_t(ny) * W + std::size_t(nx)].hit != sh[p].hit) silhouette = true;
            }
            bool rasterEdge = false;
            if (slot[p]) {
                const float* q = &st.screen[(slot[p] - 1) * 6];
                for (int e = 0; e < 3 && !rasterEdge; ++e) {
                    const float ax = q[2 * e], ay = q[2 * e + 1], bx = q[2 * ((e + 1) % 3)], by = q[2 * ((e + 1) % 3) + 1];
                    const float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float t = std::clamp(l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0.0f, 0.0f, 1.0f);
                    rasterEdge = std::hypot(ax + t * dx - px, ay + t * dy - py) <= 1.0f;
                }
            }
            (silhouette || rasterEdge ? m.edge : m.unexplained) += 1;
        }
    return m;
}
std::string composite(Ctx& c, bool& ok) {
    const swr::Scene room = swr::retroRoom();
    const sdf::Scene sc = sdf::roomSculptures();
    const SwrFrame f = swrGpu(c.dev, room.geo, room.tex, room.light, room.viewProj(W, H), W, H, swr::Options{}, nullptr);
    if (!f.error.empty()) { c.P.error = f.error; ok = false; return ""; }
    std::vector<std::uint32_t> vis(std::size_t(W) * H * 2);
    for (std::size_t p = 0; p < f.vis.slot.size(); ++p) { vis[p * 2] = f.vis.slot[p]; vis[p * 2 + 1] = f.vis.depthQ[p]; }
    const std::vector<sdf::MarchHit> sh = cpuMarch(sc);
    const swr::Setup cpuSetup = swr::setup(room.geo, room.viewProj(W, H), W, H, swr::Options{});   // screen edges (CPU only)
    std::string out;
    ok = true;
    for (std::uint32_t flags : {0u, 2u}) {
        SdfJob j; j.w = W; j.h = H; j.rasterVis = &vis; j.rasterDepth = &f.vis.depth; j.nearZ = room.nearZ; j.farZ = room.farZ; j.flags = flags;
        const SdfGpu g = sdfGpu(c.dev, sc, j);
        if (!g.error.empty()) { c.P.error = g.error; ok = false; return ""; }
        const M6 m = compareComposite(sc, room, cpuSetup, f.vis.slot, g.composite, sh);
        const bool within = m.unexplained == 0;
        ok = ok && (flags ? !within : within);
        char b[300];
        std::snprintf(b, sizeof b, "%s{\"check\": \"M6\", \"case\": \"%s\", \"agree\": %ld, \"depth_tie\": %ld, \"edge_or_silhouette\": %ld, \"unexplained\": %ld, \"within\": %s}",
                      out.empty() ? "" : ",\n  ", flags ? "control: SDF always in front" : "composite against cpu", m.agree, m.tie, m.edge, m.unexplained,
                      within ? "true" : "false");
        out += b;
    }
    return out;
}
}  // namespace

std::string SdfParity::json() const {
    return "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R3, the GPU ray marcher: M2 to M7 (evidence/rt-r3-bounds.json)\",\n"
           " \"m2_to_m5_size\": " + std::to_string(size) + ",\n \"m6_size\": 256,\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + jsonText(adapter) + "\",\n \"error\": \"" + jsonText(error) + "\",\n \"pass\": " + (pass() ? "true" : "false") +
           ",\n \"results\": [\n" + results + "\n ]\n}\n";
}

SdfParity sdfParity(rhi::Device& dev, bool quick) {
    W = H = quick ? 128 : 256;
    SdfParity P;
    P.size = W;
    P.backend = dev.backendName(); P.adapter = dev.adapter().description;
    Ctx c{dev, P};
    for (const sdf::Scene& s : sdf::ownedScenes()) {
            const std::vector<sdf::MarchHit> ref = cpuMarch(s);
            SdfJob jm; jm.w = W; jm.h = H;
        const SdfGpu g = sdfGpu(dev, s, jm);
        if (!g.error.empty()) { P.error = g.error; return P; }
        const M2 m = compareMarch(ref, g.march);
        record(c, m2Json(s.name.c_str(), "gpu against cpu", m), m.ok);
            const M3 t = shadingTerms(c, s, ref, 12.0f);
        if (!P.error.empty()) return P;
        record(c, m3Json(s.name.c_str(), "gpu against cpu", t), t.ok);
        if (s.name != "garden") continue;
        sdf::Scene half = s;                                        // M2 control: smooth-union k halved
        for (std::size_t i = 0; i + sdf::kNodeFloats <= half.prog.nodes.size(); i += sdf::kNodeFloats)
            if (int(half.prog.nodes[i]) == sdf::kSmooth) half.prog.nodes[i + 2] *= 0.5f;
        const SdfGpu gh = sdfGpu(dev, half, jm);
        if (!gh.error.empty()) { P.error = gh.error; return P; }
        const M2 mc = compareMarch(ref, gh.march);
        record(c, m2Json(s.name.c_str(), "control: smooth k halved", mc), !mc.ok);
        const M3 tc = shadingTerms(c, s, ref, 3.0f);                // M3 control: k = 3
        if (!P.error.empty()) return P;
        record(c, m3Json(s.name.c_str(), "control: soft shadow k = 3", tc), !tc.ok);
    }
    bool ok = false;
    std::string s4 = fog(c, ok);
    if (!P.error.empty()) return P;
    record(c, s4, ok);
    for (const sdf::Scene& s : {sdf::arcade(), sdf::mengerTower()}) {
            const std::string g0 = godRays(c, s, 0, "gpu against cpu", ok);
        if (!P.error.empty()) return P;
        record(c, g0, ok);
        const std::string g1 = godRays(c, s, 1, "control: sun visibility 1", ok);
        if (!P.error.empty()) return P;
        record(c, g1, ok);
    }
    W = H = 256;   // M6 always at full size: its CPU reference is cheap, and at 128 x 128 its control did not discriminate
    const std::string s6 = composite(c, ok);
    if (!P.error.empty()) return P;
    record(c, s6, ok);
    SdfJob j7; j7.w = W; j7.h = H; j7.god = true;                                       // M7: twice, bit for bit
    const sdf::Scene a = sdf::arcade();
    const SdfGpu r1 = sdfGpu(dev, a, j7), r2 = sdfGpu(dev, a, j7);
    const bool same = r1.error.empty() && r2.error.empty() && r1.march.size() == r2.march.size() && r1.god.size() == r2.god.size() &&
                      std::memcmp(r1.march.data(), r2.march.data(), r1.march.size() * 4) == 0 &&
                      std::memcmp(r1.god.data(), r2.god.data(), r1.god.size() * 4) == 0;
    record(c, std::string("{\"check\": \"M7\", \"scene\": \"arcade\", \"identical\": ") + (same ? "true" : "false") + "}", same);
    P.allPass = c.all;
    return P;
}

}  // namespace raw::gpu_check
