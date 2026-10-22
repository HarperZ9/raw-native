// raw_native_cli art tiltshift: P23's record against evidence/tiltshift-bounds.json (t1 to t6,
// d1 to d5, c1 to c3 on the CPU; g1 and g3 in art_tiltshift_gpu_cmd.cpp).
#include "art_report.hpp"
#include "raw/renderer/art_tiltshift_checks.hpp"
#include "raw/renderer/style_paint.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::art_cmd {
std::string tiltGpu(const Options& o, bool& pass);   // art_tiltshift_gpu_cmd.cpp
namespace {
using namespace raw::art;
constexpr std::uint64_t kSeed = 20261022;
constexpr double kC0 = 1.0;
const D3 kStreet{1, 0, 0}, kPlayer{0, 0.89, 0};

struct Scene { Frame f; View v; LensGeom g; std::vector<float> lens, band, paint, plain; };
Scene town(int w, int h, const std::string& look, int threads, bool lines = true) {
    Scene s;
    s.v = townView(w, h);
    s.f = renderFrame(s.v, townDusk(), threads, SceneKind::Town);
    s.g = resolveLens(lensOf(tiltLook(look), kPlayer, kStreet), s.v);
    s.lens = cocMap(s.f, s.g);
    s.band = bandCoc(s.lens, s.g, s.v, kPlayer, kC0);
    style::PaintParams pp = style::paintPreset("oil");
    if (!lines) pp.lines = 0;
    s.paint = style::runPaint(style::resolvePaint(pp, w, h), s.f.lit, nullptr);
    return s;
}
std::vector<float> cocColour(const std::vector<float>& coc, double cMax) {
    std::vector<float> o(coc.size() * 2, 1.0f);
    for (std::size_t i = 0; i < coc.size() / 2; ++i) {
        const double t = std::clamp(coc[i * 2] / cMax, 0.0, 1.0);
        o[i * 4] = float(t); o[i * 4 + 1] = float(coc[i * 2] <= kC0 ? 0.8 : 0.15 * (1 - t)); o[i * 4 + 2] = float(1 - t);
    }
    return o;
}
bool png(const Options& o, const std::string& name, int w, int h, const std::vector<float>& img) { return writeCodesPng(o.out + "/" + name + ".png", w, h, codes8(img)); }

std::string goldens(const Options& o, bool& ok) {
    const Scene s = town(256, 144, "town_map", o.threads);
    const std::vector<float> ts = paintDefocus(s.paint, s.lens, 256, 144, {}, DefocusMode::Paint, o.threads);
    Json j;
    j.str("coc_fnv", hex(fnv1a(s.lens))).str("tiltshift_fnv", hex(fnv1a(codes8(ts))));
    ok = png(o, "golden-town-256", 256, 144, ts) && ok;
    return j.done();
}

// t1 to t4 and the captures, at 960 x 540.
std::string inkAndPaint(const Options& o, bool& pass, bool& ok) {
    const int w = 960, h = 540;
    const Scene a = town(w, h, "town_map", o.threads), b = town(w, h, "town_map", o.threads, false);
    const DefocusParams dp;
    auto def = [&](const std::vector<float>& img, const std::vector<float>& coc, DefocusMode m) { return paintDefocus(img, coc, w, h, dp, m, o.threads); };
    const auto oursL = def(a.paint, a.lens, DefocusMode::Paint), oursN = def(b.paint, a.lens, DefocusMode::Paint);
    const auto bandL = def(a.paint, a.band, DefocusMode::Paint), bandN = def(b.paint, a.band, DefocusMode::Paint);
    const auto gaussL = def(a.paint, a.lens, DefocusMode::Gaussian);
    // t1: people whose every pixel is in focus
    Json people;
    std::string list = "[";
    int inWedge = 0, bandFails = 0;
    bool t1 = true;
    double yMin = 1e9, yMax = -1e9;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    for (const TownPerson& p : townPeople()) {
        std::vector<std::uint8_t> m(n, 0);
        long cnt = 0; bool sharp = true; double ys = 0;
        for (std::size_t i = 0; i < n; ++i) if (a.f.obj[i] == p.obj) { m[i] = 1; ++cnt; ys += double(i / std::size_t(w)); sharp = sharp && a.lens[i * 2] <= kC0; }
        if (cnt == 0) continue;
        const auto M = dilate(m, w, h, 2);
        const double base = inkWeight(a.paint, b.paint, M), ro = inkWeight(oursL, oursN, M) / base, rb = inkWeight(bandL, bandN, M) / base;
        Json k;
        k.integer("obj", p.obj).integer("pixels", cnt).boolean("in_wedge", sharp).num("screen_y", ys / cnt / h).num("ink_ratio_ours", ro).num("ink_ratio_band", rb);
        list += (list.size() > 1 ? "," : "") + k.done();
        if (!sharp) continue;
        ++inWedge; yMin = std::min(yMin, ys / cnt / h); yMax = std::max(yMax, ys / cnt / h);
        t1 = t1 && ro >= 0.95 && ro <= 1.05;
        bandFails += !(rb >= 0.95 && rb <= 1.05);
    }
    const bool t1Pass = t1 && inWedge >= 3 && yMax - yMin >= 0.25, bandFailsOk = bandFails >= 1;
    // t2, t3 and t4
    std::vector<std::uint8_t> far(n, 0);
    for (std::size_t i = 0; i < n; ++i) far[i] = a.lens[i * 2] >= 4.0;
    const double t2 = inkWeight(oursL, oursN, far) / inkWeight(a.paint, b.paint, far);
    const GradStats gOurs = gradStats(oursL, a.paint, far, w, h), gGauss = gradStats(gaussL, a.paint, far, w, h);
    long lensIn = 0, bandOut = 0, bandIn = 0, lensOut = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (a.lens[i * 2] <= kC0) { ++lensIn; bandOut += a.band[i * 2] > 2.5; }
        if (a.band[i * 2] <= kC0) { ++bandIn; lensOut += a.lens[i * 2] > 2.5; }
    }
    // the art scene at orbit frame 0, for t3 on a second scene and for the captures
    const View av = orbitView(0, w, h);
    const Frame af = renderFrame(av, daylight(), o.threads);
    const LensGeom ag = resolveLens(lensOf(tiltLook("town_map"), {0, 1, 0}, kStreet), av);
    TiltLook al = tiltLook("town_map"); al.streetAxis = false;
    const LensGeom ag2 = resolveLens(lensOf(al, {0, 1, 0}, kStreet), av);
    (void)ag;
    const std::vector<float> acoc = cocMap(af, ag2), aband = bandCoc(acoc, ag2, av, {0, 1, 0}, kC0);
    const std::vector<float> apaint = style::runPaint(style::resolvePaint(style::paintPreset("oil"), w, h), af.lit, nullptr);
    const auto aOurs = def(apaint, acoc, DefocusMode::Paint), aGauss = def(apaint, acoc, DefocusMode::Gaussian), aBand = def(apaint, aband, DefocusMode::Paint);
    std::vector<std::uint8_t> afar(n, 0);
    for (std::size_t i = 0; i < n; ++i) afar[i] = acoc[i * 2] >= 4.0;
    const GradStats aOursG = gradStats(aOurs, apaint, afar, w, h), aGaussG = gradStats(aGauss, apaint, afar, w, h);
    const bool t2Pass = t2 <= 0.5;
    const bool t3Pass = gOurs.D <= 0.7 && gOurs.E >= 0.5 && aOursG.D <= 0.7 && aOursG.E >= 0.5, t3Control = gGauss.E < 0.5 && aGaussG.E < 0.5;
    pass = pass && t1Pass && bandFailsOk && t2Pass && t3Pass && t3Control;
    // captures
    ok = png(o, "town-plain", w, h, a.paint) && png(o, "town-band", w, h, bandL) && png(o, "town-gaussian", w, h, gaussL) && png(o, "town-ours", w, h, oursL) &&
         png(o, "town-coc", w, h, cocColour(a.lens, a.g.cMax)) && png(o, "town-band-coc", w, h, cocColour(a.band, a.g.cMax)) && png(o, "art-plain", w, h, apaint) &&
         png(o, "art-band", w, h, aBand) && png(o, "art-gaussian", w, h, aGauss) && png(o, "art-ours", w, h, aOurs) && png(o, "art-coc", w, h, cocColour(acoc, ag2.cMax)) && ok;
    for (const char* look : {"daily", "town_map", "record", "memory", "dream", "night"}) {
        const Scene s = town(w, h, look, o.threads);
        const auto d = def(s.paint, s.lens, DefocusMode::Paint);
        ok = png(o, std::string("town-look-") + look, w, h, fxStack(planFx(tiltLook(look), {0, false, false}, w, h, s.v.focalPx()), d)) && ok;
    }
    auto gs = [](const GradStats& g) { Json k; k.num("D", g.D).num("E", g.E).integer("pixels", g.n); return k.done(); };
    Json j;
    j.raw("people", list + "]").integer("in_wedge", inWedge).num("in_wedge_y_span", yMax - yMin).boolean("t1_pass", t1Pass).integer("band_people_failing", bandFails)
     .boolean("t1_band_control_fails", bandFailsOk).num("t2_ink_ratio_far", t2).boolean("t2_pass", t2Pass)
     .raw("t3_town_ours", gs(gOurs)).raw("t3_town_gaussian", gs(gGauss)).raw("t3_art_ours", gs(aOursG)).raw("t3_art_gaussian", gs(aGaussG))
     .boolean("t3_pass", t3Pass).boolean("t3_gaussian_control_fails", t3Control)
     .num("t4_lens_in_focus_band_defocused", double(bandOut) / std::max(1L, lensIn)).num("t4_band_in_focus_lens_defocused", double(lensOut) / std::max(1L, bandIn))
     .num("hinge_tilt_deg", hingeTiltDeg(a.g)).num("cmax_px", a.g.cMax);
    std::printf("art tiltshift: t1 %d in wedge, span %.2f, pass %d, band fails %d; t2 %.3f; t3 ours D %.2f E %.2f, gauss E %.2f\n", inWedge, yMax - yMin, t1Pass, bandFails, t2,
                gOurs.D, gOurs.E, gGauss.E);
    return j.done();
}

std::string director(bool& pass) {
    const DirectorReport r = runDirectorChecks();
    bool dw = r.dwells.size() >= 2, rt = !r.returns.empty();
    for (double d : r.dwells) dw = dw && d >= 2 && d <= 6;
    for (double t : r.returns) rt = rt && t <= 2;
    const bool d1 = r.maxOvershoot <= 1e-9 && r.controlOvershoot > 1e-3, d2 = r.lockFails == 0 && r.controlLockFails >= 1 && r.eventsInLocks >= 3 && r.idleInLocks >= 20;
    const bool d3 = r.drifts >= 2 && dw && rt && r.calmDrifts == 0 && r.calmAnimated == 0, d4 = r.eventLatency >= 0 && r.eventLatency <= 0.25 && r.eventSettle >= 0 && r.eventSettle <= 1.0;
    const bool d5 = r.maxJitterStep <= 0.02 && r.jitterHz <= 0.5 && r.calmJitter == 0;
    pass = pass && d1 && d2 && d3 && d4 && d5;
    Json j;
    j.num("max_overshoot", r.maxOvershoot).num("underdamped_overshoot", r.controlOvershoot).boolean("d1_pass", d1)
     .integer("lock_frames_checked", r.lockFramesChecked).integer("lock_fails", r.lockFails).integer("lock_off_control_fails", r.controlLockFails)
     .integer("events_in_locks", r.eventsInLocks).num("idle_s_in_locks", r.idleInLocks).boolean("d2_pass", d2)
     .integer("drifts", r.drifts).nums("dwells_s", r.dwells).nums("returns_s", r.returns).integer("calm_drifts", r.calmDrifts).integer("calm_animated_frames", r.calmAnimated).boolean("d3_pass", d3)
     .num("event_latency_s", r.eventLatency).num("event_settle_s", r.eventSettle).boolean("d4_pass", d4)
     .num("child_max_jitter_step_dioptre", r.maxJitterStep).num("child_jitter_hz", r.jitterHz).num("calm_jitter", r.calmJitter).boolean("d5_pass", d5);
    std::printf("art tiltshift: director d1 %d d2 %d (fails %d, control %d) d3 %d (drifts %d) d4 %d d5 %d\n", d1, d2, r.lockFails, r.controlLockFails, d3, r.drifts, d4, d5);
    return j.done();
}

// c1 to c3: the night look's stack over a rack on the town (the camera held, the director moving
// from the player to the lit window and back), 48 frames at 24 a second.
std::string care(const Options& o, bool& pass) {
    const int w = 480, h = 270, F = 48;
    const Scene s = town(w, h, "night", o.threads);
    focus::Director d(focus::Character::Naturalist, lensOf(tiltLook("night"), kPlayer, kStreet), s.v);
    std::vector<std::vector<float>> in, out;
    int red = 0; long redPx = 0, redPxIn = 0;
    for (int t = 0; t < F; ++t) {
        focus::Inputs fi;
        fi.player = kPlayer; fi.prop = {6.6, 2.9, -4.98}; fi.playerMoving = true;
        fi.intent = t >= 16 && t < 32 ? focus::Intent::Examine : focus::Intent::Walk;
        d.step(fi, 1.0 / 24);
        const auto def = paintDefocus(s.paint, cocMap(s.f, d.geom()), w, h, {}, DefocusMode::Paint, o.threads);
        const auto fx = fxStack(planFx(tiltLook("night"), {t, false, false}, w, h, s.v.focalPx()), def);
        red += redWindows(fx, w, h) > redWindows(def, w, h) ? 1 : 0;
        redPx = std::max(redPx, redPixels(fx)); redPxIn = std::max(redPxIn, redPixels(def));
        in.push_back(def); out.push_back(fx);
    }
    const int fin = maxFlashes(in, w, h, 24), fout = maxFlashes(out, w, h, 24);
    TiltLook mem = tiltLook("memory");
    const FxPlan c0 = planFx(mem, {0, true, false}, w, h, s.v.focalPx()), c1 = planFx(mem, {1, true, false}, w, h, s.v.focalPx());
    const bool grainStill = fxStack(c0, s.paint) == fxStack(c1, s.paint);
    const bool c3 = c0.weaveX == 0 && c0.weaveY == 0 && c1.weaveX == 0 && c1.weaveY == 0 && grainStill;
    pass = pass && fout <= fin && red == 0 && c3;
    Json j;
    j.integer("flashes_in", fin).integer("flashes_out", fout).boolean("c1_pass", fout <= fin).integer("red_windows_added_frames", red)
     .integer("red_pixels_max_out", redPx).integer("red_pixels_max_in", redPxIn).boolean("c2_pass", red == 0).boolean("grain_still", grainStill).boolean("c3_pass", c3);
    std::printf("art tiltshift: care flashes in %d out %d, red %d, c3 %d\n", fin, fout, red, c3);
    return j.done();
}

std::string temporal(const Options& o, bool& pass) {
    Json j;
    for (int rack = 0; rack < 2; ++rack) {
        const TemporalRun r = runTiltOrbit(480, 270, o.frames, 9, o.threads, kSeed, rack == 1);
        const double rel = r.oursVsBase.meanA / r.oursVsBase.meanB - 1;
        const bool ok = rel <= 0.05 && !(r.oursVsBase.diffLo > 0);
        pass = pass && ok;
        Json k;
        k.raw("ours_vs_base", pairedJson(r.oursVsBase)).num("relative_change", rel).boolean("pass", ok).nums("ours", r.ours).nums("base", r.base);
        j.raw(rack ? "t6_rack" : "t5_orbit", k.done());
        std::printf("art tiltshift: %s ours %.5f base %.5f change %+.3f [%.5f, %.5f]\n", rack ? "rack" : "orbit", r.oursVsBase.meanA, r.oursVsBase.meanB, rel, r.oursVsBase.diffLo, r.oursVsBase.diffHi);
    }
    return j.done();
}
}  // namespace

int tiltshift(const Options& o) {
    bool pass = true, ok = true;
    Json j;
    j.str("schema", "raw-native.evidence/1").str("bounds", "evidence/tiltshift-bounds.json (with addendum A)").raw("goldens", goldens(o, ok));
    if (o.cpu) j.raw("director", director(pass)).raw("care", care(o, pass)).raw("ink_and_paint", inkAndPaint(o, pass, ok));
    if (o.cpu && o.orbit) j.raw("temporal", temporal(o, pass));
    if (o.gpu || o.cost) j.raw("gpu", tiltGpu(o, pass));
    j.boolean("pass", pass && ok);
    const std::string doc = j.done();
    if (!writeText(o.out + "/tiltshift.json", doc)) return 2;
    std::printf("art tiltshift: pass %d\n", pass && ok);
    return pass && ok ? 0 : 1;
}

}  // namespace raw::art_cmd
