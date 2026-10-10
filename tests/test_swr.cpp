// The software rasterizer's CPU checks (evidence/rt-r1-bounds.json): C1 watertight fill on
// shared-edge meshes with its two controls, C2 visibility against BVH ray casts with the
// depth-test control, C3 perspective-correct UVs against float64 ray-plane answers with the
// affine control.
//   test_swr [out.json]
#include "raw/renderer/swr.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/renderer/bvh.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace raw;
using namespace raw::swr;

namespace {
std::string js;   // the evidence body, one entry per line
void add(const std::string& s) { js += (js.empty() ? "  " : ",\n  ") + s; }

// ---- C1 ----
struct Fill { long doubles{0}, holes{0}, outside{0}, boundary{0}, inside{0}, folded{0}; };
Fill fill(const ScreenMesh& m, FillRule rule, int snapShift) {
    Options o; o.rule = rule; o.snapShift = snapShift;
    const int W = 256, H = 256;
    const Visibility v = rasterize(setupScreen(m.corners, W, H, o), o);
    const float scale = std::ldexp(1.0f, kSubpixelBits - snapShift);
    const std::int32_t grid = std::int32_t(1) << snapShift;
    std::vector<std::int32_t> ox, oy;
    for (Vec2 p : m.outline) { ox.push_back(std::int32_t(std::floor(p.x * scale + 0.5f)) * grid); oy.push_back(std::int32_t(std::floor(p.y * scale + 0.5f)) * grid); }
    Fill f;
    // Precondition: a valid planar mesh, every input triangle wound the same way (a folded
    // triangle covers its neighbours' pixels twice under any fill rule).
    long pos = 0, neg = 0;
    for (std::size_t t = 0; t + 2 < m.corners.size(); t += 3) {
        std::int32_t q[6];
        for (int k = 0; k < 3; ++k) { q[2 * k] = std::int32_t(std::floor(m.corners[t + k].x * scale + 0.5f)) * grid; q[2 * k + 1] = std::int32_t(std::floor(m.corners[t + k].y * scale + 0.5f)) * grid; }
        const std::int64_t a = edge(q[0], q[1], q[2], q[3], q[4], q[5]);
        (a > 0 ? pos : neg) += 1;
    }
    f.folded = std::min(pos, neg);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::int32_t px = x * 256 + 128, py = y * 256 + 128;
            int wn = 0;
            bool on = false;
            for (std::size_t i = 0; i < ox.size(); ++i) {
                const std::size_t j = (i + 1) % ox.size();
                const std::int64_t e = edge(ox[i], oy[i], ox[j], oy[j], px, py);
                if (e == 0 && px >= std::min(ox[i], ox[j]) && px <= std::max(ox[i], ox[j]) &&
                    py >= std::min(oy[i], oy[j]) && py <= std::max(oy[i], oy[j])) on = true;
                if (oy[i] <= py) { if (oy[j] > py && e > 0) ++wn; }
                else if (oy[j] <= py && e < 0) --wn;
            }
            const std::uint32_t c = v.count[std::size_t(y * W + x)];
            if (c > 1) ++f.doubles;
            if (on) { ++f.boundary; continue; }
            if (wn != 0) { ++f.inside; if (c == 0) ++f.holes; }
            else if (c > 0) ++f.outside;
        }
    return f;
}
void c1() {
    for (const ScreenMesh& m : {tieGrid(false), tieGrid(true), tieFan()}) {
        const Fill a = fill(m, FillRule::TopLeft, 0), inc = fill(m, FillRule::Inclusive, 0), exc = fill(m, FillRule::Exclusive, 0);
        const Fill px = fill(m, FillRule::TopLeft, 8);
        std::printf("C1 %-18s inside %ld boundary %ld: doubles %ld holes %ld outside %ld | inclusive doubles %ld | exclusive holes %ld | whole-pixel snap doubles %ld holes %ld\n",
                    m.name.c_str(), a.inside, a.boundary, a.doubles, a.holes, a.outside, inc.doubles, exc.holes, px.doubles, px.holes);
        CHECK(a.folded == 0 && px.folded == 0);
        CHECK(a.inside > 10000);
        CHECK(a.doubles == 0 && a.holes == 0 && a.outside == 0);
        CHECK(inc.doubles > 0);
        CHECK(exc.holes > 0);
        char b[512];
        std::snprintf(b, sizeof b, "{\"check\": \"C1\", \"mesh\": \"%s\", \"inside\": %ld, \"boundary_exempt\": %ld, \"doubles\": %ld, \"holes\": %ld, \"outside\": %ld, "
                      "\"control_inclusive_doubles\": %ld, \"control_exclusive_holes\": %ld, \"reported_whole_pixel_snap\": {\"doubles\": %ld, \"holes\": %ld, \"outside\": %ld}}",
                      m.name.c_str(), a.inside, a.boundary, a.doubles, a.holes, a.outside, inc.doubles, exc.holes, px.doubles, px.holes, px.outside);
        add(b);
    }
}

// ---- C2 ----
struct Cam { Vec3 f, s, u; float th, aspect; };
Cam camera(const Scene& sc, int w, int h) {
    Cam c;
    c.f = normalize(sc.target - sc.eye); c.s = normalize(cross(c.f, sc.up)); c.u = cross(c.s, c.f);
    c.th = std::tan(sc.fovy * 0.5f); c.aspect = float(w) / float(h);
    return c;
}
Vec3 rayDir(const Cam& c, int x, int y, int w, int h) {
    const float nx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f, ny = 1.0f - (float(y) + 0.5f) / float(h) * 2.0f;
    return c.f + c.s * (nx * c.th * c.aspect) + c.u * (ny * c.th);
}
float segDist(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    float t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = ax + t * dx - px, ey = ay + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}
bool nearEdge(const Setup& s, int src, float px, float py) {
    if (src < 0) return false;
    for (int k = 0; k < kSlotsPerTri; ++k) {
        const std::size_t slot = std::size_t(src) * kSlotsPerTri + std::size_t(k);
        if (s.ints[slot * kSetupInts + 6] == 0) continue;
        const float* q = &s.screen[slot * 6];
        for (int e = 0; e < 3; ++e) {
            const int a = e, b = (e + 1) % 3;
            if (segDist(px, py, q[2 * a], q[2 * a + 1], q[2 * b], q[2 * b + 1]) <= 1.0f) return true;
        }
    }
    return false;
}
struct Vis { long pixels{0}, covered{0}, mismatches{0}, edgeExempt{0}, tieExempt{0}, planeExempt{0}; };
Vis visibility(const Scene& sc, const std::vector<Tri>& tris, const Bvh& bvh, bool depthTest) {
    const int W = 256, H = 256;
    Options o; o.depthTest = depthTest;
    const Setup s = setup(sc.geo, sc.viewProj(W, H), W, H, o);
    const Visibility v = rasterize(s, o);
    const Cam c = camera(sc, W, H);
    Vis r;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            ++r.pixels;
            const std::uint32_t slot = v.slot[std::size_t(y * W + x)];
            const int rs = slot ? int((slot - 1) / kSlotsPerTri) : -1;
            if (slot) ++r.covered;
            const Vec3 d = rayDir(c, x, y, W, H);
            Hit h;
            const bool hit = bvh.closest({sc.eye, d}, 1e30f, h);
            const int hs = hit ? h.tri : -1;
            if (rs == hs) continue;
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            if (hit && (h.t < sc.nearZ || h.t > sc.farZ)) { ++r.planeExempt; continue; }
            if (nearEdge(s, rs, px, py) || nearEdge(s, hs, px, py)) { ++r.edgeExempt; continue; }
            if (hit && rs >= 0) {
                const Tri& t = tris[std::size_t(rs)];
                const Vec3 n = cross(t.b - t.a, t.c - t.a);
                const float tr = dot(n, t.a - sc.eye) / dot(n, d);
                if (std::fabs(tr - h.t) <= 1e-4f * h.t) { ++r.tieExempt; continue; }
            }
            ++r.mismatches;
        }
    return r;
}
void c2() {
    for (const Scene& sc : ownedScenes()) {
        std::vector<Tri> tris;
        for (std::size_t t = 0; t < sc.geo.triangles(); ++t)
            tris.push_back({sc.geo.pos[sc.geo.idx[t * 3]], sc.geo.pos[sc.geo.idx[t * 3 + 1]], sc.geo.pos[sc.geo.idx[t * 3 + 2]]});
        Bvh bvh;
        bvh.build(tris);
        const Vis a = visibility(sc, tris, bvh, true), ctl = visibility(sc, tris, bvh, false);
        std::printf("C2 %-12s %zu tris, covered %ld of %ld: mismatches %ld (exempt: edge %ld, tie %ld, plane %ld) | control (no depth test) mismatches %ld\n",
                    sc.name.c_str(), tris.size(), a.covered, a.pixels, a.mismatches, a.edgeExempt, a.tieExempt, a.planeExempt, ctl.mismatches);
        CHECK(a.mismatches == 0);
        CHECK(ctl.mismatches > 0);
        CHECK(a.covered > a.pixels / 4);
        char b[512];
        std::snprintf(b, sizeof b, "{\"check\": \"C2\", \"scene\": \"%s\", \"triangles\": %zu, \"pixels\": %ld, \"covered\": %ld, \"mismatches\": %ld, "
                      "\"exempt_edge\": %ld, \"exempt_depth_tie\": %ld, \"exempt_near_far\": %ld, \"control_no_depth_test_mismatches\": %ld}",
                      sc.name.c_str(), tris.size(), a.pixels, a.covered, a.mismatches, a.edgeExempt, a.tieExempt, a.planeExempt, ctl.mismatches);
        add(b);
    }
}

// ---- C3 ----
struct Persp { long pixels{0}; double maxErr{0}, median{0}; };
Persp perspective(bool affine) {
    const Scene sc = floorQuad();
    const int W = 256, H = 256;
    Options o; o.affine = affine;
    const Setup s = setup(sc.geo, sc.viewProj(W, H), W, H, o);
    const Visibility v = rasterize(s, o);
    const Resolved r = resolve(s, v, sc.geo, sc.tex, sc.light, o);
    const Cam c = camera(sc, W, H);
    std::vector<double> err;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t p = std::size_t(y * W + x);
            if (!v.slot[p]) continue;
            const Vec3 d = rayDir(c, x, y, W, H);
            const double t = -double(sc.eye.y) / double(d.y);
            const double hx = double(sc.eye.x) + double(d.x) * t, hz = double(sc.eye.z) + double(d.z) * t;
            const double u = (hx + 1.0) / 2.0, vv = 1.0 + hz / 10.0;
            err.push_back(std::max(std::fabs(r.uv[p * 2] - u), std::fabs(r.uv[p * 2 + 1] - vv)));
        }
    Persp out;
    out.pixels = long(err.size());
    if (err.empty()) return out;
    std::sort(err.begin(), err.end());
    out.maxErr = err.back();
    out.median = err[err.size() / 2];
    return out;
}
void c3() {
    const Persp a = perspective(false), ctl = perspective(true);
    std::printf("C3 floor_quad %ld pixels: max |uv err| %.3e median %.3e | control (affine) max %.3e\n", a.pixels, a.maxErr, a.median, ctl.maxErr);
    CHECK(a.pixels > 5000);
    CHECK(a.maxErr <= 1e-3 && a.median <= 1e-4);
    CHECK(ctl.maxErr > 1e-3);
    char b[256];
    std::snprintf(b, sizeof b, "{\"check\": \"C3\", \"pixels\": %ld, \"max_uv_error\": %.4e, \"median_uv_error\": %.4e, \"control_affine_max_uv_error\": %.4e}",
                  a.pixels, a.maxErr, a.median, ctl.maxErr);
    add(b);
}
}  // namespace

int main(int argc, char** argv) {
    c1();
    c2();
    c3();
    if (argc > 1) {
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R1, CPU checks C1 to C3 (evidence/rt-r1-bounds.json)\",\n"
                               << " \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << ",\n \"results\": [\n" << js << "\n ]\n}\n";
    }
    return raw_test_summary();
}
