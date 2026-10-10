// The GPU rasterizer against the CPU reference: checks C4 and C5 of evidence/rt-r1-bounds.json.
#include "raw/renderer/swr_parity.hpp"
#include "json_text.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace raw::gpu_check {
namespace {
using namespace raw::swr;
struct Mode { const char* name; Options o; int w, h; };
std::vector<Mode> modes() {
    Mode m0{"modern", {}, 256, 256}, m1{"affine", {}, 256, 256}, m2{"snap_whole_pixel_320x240", {}, 320, 240}, m3{"dither_depth_12", {}, 256, 256};
    m1.o.affine = true;
    m2.o.snapShift = 8; m2.o.affine = true;
    m3.o.depthBits = 12;
    return {m0, m1, m2, m3};
}
std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool sameSlot(const Setup& a, const Setup& b, std::size_t s) {
    for (int k = 0; k < kSetupInts; ++k) if (a.ints[s * kSetupInts + k] != b.ints[s * kSetupInts + k]) return false;
    for (int k = 0; k < kSetupFloats; ++k) if (bits(a.floats[s * kSetupFloats + k]) != bits(b.floats[s * kSetupFloats + k])) return false;
    return true;
}
bool nearInteger(float v) { return std::fabs(v - std::floor(v + 0.5f)) < 1e-3f; }
// Per-pixel comparison shared by C4 and C5. `differs` marks slots whose setup differs (C5).
void comparePixels(SwrCase& c, const Visibility& cv, const Resolved& cr, const Visibility& gv, const Resolved& gr, const Options& o,
                   const std::vector<char>* differs) {
    const std::size_t n = cv.slot.size();
    c.pixels = long(n);
    for (std::size_t p = 0; p < n; ++p) {
        const std::uint32_t a = cv.slot[p], b = gv.slot[p];
        if (a) ++c.covered;
        const long ta = a ? long((a - 1) / kSlotsPerTri) : -1, tb = b ? long((b - 1) / kSlotsPerTri) : -1;
        const bool slotDiff = differs && ((a && (*differs)[a - 1]) || (b && (*differs)[b - 1]));
        if (a != b) ++c.slotMismatch;
        if (ta != tb) {
            ++c.triMismatch;
            if (differs) (slotDiff ? c.pixelsExplained : c.pixelsUnexplained) += 1;
            continue;
        }
        if (!a || a != b) continue;
        const bool exact = !differs || !slotDiff;
        const double dd = std::fabs(double(cv.depth[p]) - double(gv.depth[p]));
        c.worstDepth = std::max(c.worstDepth, dd);
        if (exact) {
            if (bits(cv.depth[p]) != bits(gv.depth[p]) || (o.depthBits > 0 && cv.depthQ[p] != gv.depthQ[p])) ++c.depthMismatch;
        } else if (dd > 1e-6) {
            ++c.depthOverBound;
        }
        for (int k = 0; k < 2; ++k) {
            const double u = cr.uv[p * 2 + std::size_t(k)], g = gr.uv[p * 2 + std::size_t(k)];
            const double r = std::fabs(u - g) / (1e-5 * (1.0 + std::fabs(u)));
            c.worstUv = std::max(c.worstUv, r);
            if (r > 1.0) ++c.uvOutside;
        }
        if (cr.texel[p] != gr.texel[p]) {
            if (nearInteger(cr.texelCoord[p * 2]) || nearInteger(cr.texelCoord[p * 2 + 1])) ++c.texelExempt;
            else ++c.texelMismatch;
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            const int x = int((cr.colour[p] >> (8 * k)) & 255u), y = int((gr.colour[p] >> (8 * k)) & 255u);
            if (std::abs(x - y) > 1) { ++c.colourOutside; break; }
        }
    }
}
long pixelDiff(const Visibility& a, const Visibility& b) {
    long d = 0;
    for (std::size_t p = 0; p < a.slot.size(); ++p)
        if (a.slot[p] != b.slot[p] || (a.slot[p] && bits(a.depth[p]) != bits(b.depth[p]))) ++d;
    return d;
}
}  // namespace

std::string SwrCase::json() const {
    char b[1400];
    std::snprintf(b, sizeof b,
        "{\"check\": \"%s\", \"scene\": \"%s\", \"mode\": \"%s\", \"pixels\": %ld, \"covered\": %ld, \"triangle_id_mismatches\": %ld, "
        "\"slot_mismatches\": %ld, \"depth_bit_mismatches\": %ld, \"uv_outside\": %ld, \"worst_uv_ratio\": %.4f, \"texel_mismatches\": %ld, "
        "\"texel_boundary_exempt\": %ld, \"colour_outside\": %ld, \"valid_slots_cpu\": %ld, \"valid_slots_gpu\": %ld, \"valid_set_differences\": %ld, "
        "\"slots_differing\": %ld, \"coords_over_one_step\": %ld, \"pixels_unexplained\": %ld, \"pixels_explained\": %ld, "
        "\"depth_over_1e-6\": %ld, \"worst_depth_abs\": %.3e, \"pass\": %s}",
        check.c_str(), scene.c_str(), mode.c_str(), pixels, covered, triMismatch, slotMismatch, depthMismatch, uvOutside, worstUv,
        texelMismatch, texelExempt, colourOutside, validCpu, validGpu, validSetDiff, slotsDiffering, coordOverStep, pixelsUnexplained,
        pixelsExplained, depthOverBound, worstDepth, pass ? "true" : "false");
    return b;
}
bool SwrParity::pass() const {
    if (!error.empty() || cases.empty()) return false;
    for (const SwrCase& c : cases) if (!c.pass) return false;
    return controlFillRuleDiff > 0 && controlMovedVertexDiff > 0 && controlGuardBandSlotDiff > 0;
}
std::string SwrParity::json() const {
    std::string s = "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R1, GPU checks C4 and C5 (evidence/rt-r1-bounds.json)\",\n";
    s += " \"backend\": \"" + backend + "\",\n \"adapter\": \"" + jsonText(adapter) + "\",\n \"error\": \"" + jsonText(error) + "\",\n";
    s += " \"control_flipped_fill_rule_pixels_differing\": " + std::to_string(controlFillRuleDiff) + ",\n";
    s += " \"control_moved_vertex_pixels_differing\": " + std::to_string(controlMovedVertexDiff) + ",\n";
    s += " \"reported_moved_vertex_uv_components_differing\": " + std::to_string(controlMovedVertexUvDiff) + ",\n";
    s += " \"control_guard_band_8_valid_slot_differences\": " + std::to_string(controlGuardBandSlotDiff) + ",\n";
    s += " \"pass\": " + std::string(pass() ? "true" : "false") + ",\n \"cases\": [\n";
    for (std::size_t i = 0; i < cases.size(); ++i) s += "  " + cases[i].json() + (i + 1 < cases.size() ? ",\n" : "\n");
    return s + " ]\n}\n";
}

namespace {
bool fail(SwrParity& R, const SwrFrame& f) {
    if (!f.error.empty() && R.error.empty()) R.error = f.error;
    return !f.error.empty();
}
bool c4Pass(const SwrCase& c) { return c.triMismatch == 0 && c.depthMismatch == 0 && c.uvOutside == 0 && c.texelMismatch == 0 && c.colourOutside == 0; }
// C4 on the screen meshes: shared setup, every mode's rasterization options; the fill-rule control.
bool screenCases(rhi::Device& dev, SwrParity& R) {
    for (const ScreenMesh& m : {tieGrid(false), tieGrid(true), tieFan()})
        for (const Mode& md : modes()) {
            const Setup s = setupScreen(m.corners, md.w, md.h, md.o);
            const Geometry g = screenGeometry(m, float(md.w), float(md.h));
            const TextureSet t = proceduralTextures();
            const Visibility cv = rasterize(s, md.o);
            const Resolved cr = resolve(s, cv, g, t, {0, 0, 1}, md.o);
            const SwrFrame f = swrGpu(dev, g, t, {0, 0, 1}, Mat4{}, md.w, md.h, md.o, &s);
            if (fail(R, f)) return false;
            SwrCase c; c.check = "C4"; c.scene = m.name; c.mode = md.name;
            comparePixels(c, cv, cr, f.vis, f.res, md.o, nullptr);
            c.pass = c4Pass(c);
            R.cases.push_back(c);
            if (m.name == "tie_grid" && std::string(md.name) == "modern") {   // control: the tie flipped on the GPU
                Options flip = md.o; flip.rule = FillRule::BottomRight;
                const SwrFrame fc = swrGpu(dev, g, t, {0, 0, 1}, Mat4{}, md.w, md.h, flip, &s);
                if (fail(R, fc)) return false;
                R.controlFillRuleDiff = pixelDiff(cv, fc.vis);
            }
        }
    return true;
}
// C5 for one scene and mode: the GPU's own setup against the CPU's.
SwrCase c5(const SwrFrame& f5, const Setup& s, const Visibility& cv, const Resolved& cr, const char* scene, const Mode& md) {
    SwrCase e; e.check = "C5"; e.scene = scene; e.mode = md.name;
    std::vector<char> differs(s.slots(), 0);
    const std::int32_t step = std::int32_t(1) << md.o.snapShift;
    for (std::size_t k = 0; k < s.slots(); ++k) {
        const bool vc = s.ints[k * kSetupInts + 6] != 0, vg = f5.setup.ints[k * kSetupInts + 6] != 0;
        e.validCpu += vc; e.validGpu += vg;
        if (vc != vg) { ++e.validSetDiff; differs[k] = 1; continue; }
        if (!vc) continue;
        if (!sameSlot(s, f5.setup, k)) { differs[k] = 1; ++e.slotsDiffering; }
        for (int q = 0; q < 6; ++q)
            if (std::abs(s.ints[k * kSetupInts + q] - f5.setup.ints[k * kSetupInts + q]) > step) ++e.coordOverStep;
    }
    comparePixels(e, cv, cr, f5.vis, f5.res, md.o, &differs);
    e.pass = e.validSetDiff == 0 && e.coordOverStep == 0 && double(e.slotsDiffering) <= 0.01 * double(e.validCpu) &&
             e.pixelsUnexplained == 0 && e.depthMismatch == 0 && e.depthOverBound == 0;
    return e;
}
}  // namespace

SwrParity swrParity(rhi::Device& dev) {
    SwrParity R;
    R.backend = dev.backendName(); R.adapter = dev.adapter().description;
    if (!screenCases(dev, R)) return R;
    for (const Scene& sc : ownedScenes())
        for (const Mode& md : modes()) {
            const Mat4 M = sc.viewProj(md.w, md.h);
            const Setup s = setup(sc.geo, M, md.w, md.h, md.o);
            const Visibility cv = rasterize(s, md.o);
            const Resolved cr = resolve(s, cv, sc.geo, sc.tex, sc.light, md.o);
            const SwrFrame f4 = swrGpu(dev, sc.geo, sc.tex, sc.light, M, md.w, md.h, md.o, &s);
            if (fail(R, f4)) return R;
            SwrCase c; c.check = "C4"; c.scene = sc.name; c.mode = md.name;
            comparePixels(c, cv, cr, f4.vis, f4.res, md.o, nullptr);
            c.pass = c4Pass(c);
            R.cases.push_back(c);
            if (sc.name == "retro_room" && std::string(md.name) == "modern") {   // control: one vertex moved one unit
                Setup moved = s;
                const std::uint32_t centre = cv.slot[std::size_t(md.h / 2) * std::size_t(md.w) + std::size_t(md.w / 2)];
                if (centre) moved.ints[(centre - 1) * kSetupInts] += 1;
                const SwrFrame fc = swrGpu(dev, sc.geo, sc.tex, sc.light, M, md.w, md.h, md.o, &moved);
                if (fail(R, fc)) return R;
                R.controlMovedVertexDiff = pixelDiff(cv, fc.vis);
                for (std::size_t p = 0; p < cr.uv.size(); ++p) R.controlMovedVertexUvDiff += bits(cr.uv[p]) != bits(fc.res.uv[p]);
            }
            // C5: the GPU's own setup.
            const SwrFrame f5 = swrGpu(dev, sc.geo, sc.tex, sc.light, M, md.w, md.h, md.o, nullptr);
            if (fail(R, f5)) return R;
            R.cases.push_back(c5(f5, s, cv, cr, sc.name.c_str(), md));
            if (sc.name == "clip_stress" && std::string(md.name) == "modern") {   // control: guard band 8 w on the GPU
                Options gb = md.o; gb.guardBand = 8.0f;
                const SwrFrame fc = swrGpu(dev, sc.geo, sc.tex, sc.light, M, md.w, md.h, gb, nullptr);
                if (fail(R, fc)) return R;
                for (std::size_t k = 0; k < s.slots(); ++k)
                    if ((s.ints[k * kSetupInts + 6] != 0) != (fc.setup.ints[k * kSetupInts + 6] != 0)) ++R.controlGuardBandSlotDiff;
            }
        }
    return R;
}

}  // namespace raw::gpu_check
