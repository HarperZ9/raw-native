// raw_native_cli art room: the conversation room (raw/renderer/art_room.hpp) filmed through P23's
// tilt-shift lens as an image sequence for a film. Each shot is a short script of camera, focus
// and room-state keys; frames stream one at a time (trace, advected paint, painted defocus, the
// look's effect stack) to <out>/<shot>/f0000.png, with <out>/<shot>/labels.jsonl giving each
// group's screen position a frame, so a compositor can place type beside the figures.
// A rerun skips a shot whose last frame is already on disk; a shot restarts from its first frame.
#include "art_report.hpp"
#include "raw/renderer/art_metrics.hpp"
#include "raw/renderer/art_room.hpp"
#include "raw/renderer/art_tiltshift.hpp"
#include "raw/renderer/style_paint.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
namespace raw::art_cmd {
namespace {
using namespace raw::art;
constexpr double kPi = 3.141592653589793;

double ease(double u) { u = std::clamp(u, 0.0, 1.0); return u * u * u * (u * (u * 6 - 15) + 10); }
D3 lerp(const D3& a, const D3& b, double t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }
double ramp(double t, double t0, double t1) { return ease((t - t0) / (t1 - t0)); }

// The three-quarter camera on the board: azimuth and elevation in degrees about `target`.
struct Cam { D3 eye, target; double fovy; };
Cam orbitCam(const D3& target, double azDeg, double elDeg, double dist, double fovy) {
    const double az = azDeg * kPi / 180, el = elDeg * kPi / 180;
    return {{target[0] + dist * std::cos(el) * std::sin(az), target[1] + dist * std::sin(el), target[2] + dist * std::cos(el) * std::cos(az)}, target, fovy};
}
Cam mixCam(const Cam& a, const Cam& b, double t) { return {lerp(a.eye, b.eye, t), lerp(a.target, b.target, t), a.fovy + (b.fovy - a.fovy) * t}; }

struct Key { Cam cam; D3 focus; RoomState room; std::string look{"town_map"}; };
struct Shot { const char* name; double seconds; Key (*at)(double t); };

const D3 kBoardCentre{1.2, 0.4, -1.4};
Cam wide() { return orbitCam(kBoardCentre, 28, 34, 30, 0.50); }

// 1. The slot: the camera starts at one reply, the way a feed shows it, and pulls back to the room.
Key slot(double t) {
    const RoomFigure& one = roomFigures()[9];
    const D3 head{one.foot[0], 1.55, one.foot[2]};
    const Cam close{{head[0] + 1.3, 1.9, head[2] + 3.4}, head, 0.42};
    const double u = ramp(t, 1.4, 6.6);
    Key k;
    k.cam = mixCam(close, wide(), u);
    k.focus = lerp(head, roomCentre(RoomGroup::Poster), ramp(t, 4.0, 6.8));
    k.room.t = 0;
    return k;
}
// 2. The burst: the room held; a block of identical figures lands within three seconds.
Key burst(double t) {
    Key k;
    k.cam = mixCam(wide(), orbitCam(kBoardCentre, 22, 35, 28, 0.50), t / 7.0);
    k.room.burstAt = 1.6; k.room.t = t;
    k.focus = lerp(roomCentre(RoomGroup::Ordinary), roomCentre(RoomGroup::Burst), ramp(t, 1.0, 2.4));
    return k;
}
// 3. The paid voices: the disclosed partnerships take their mark, then the unlabelled look-alikes.
Key paid(double t) {
    Key k;
    k.cam = mixCam(orbitCam(kBoardCentre, 22, 35, 28, 0.50), orbitCam(kBoardCentre, 16, 37, 27, 0.50), t / 6.0);
    k.room.burstAt = -100; k.room.t = t;
    k.room.paidMark = ramp(t, 0.8, 1.8); k.room.lookalikeMark = ramp(t, 3.0, 4.0);
    k.focus = lerp(roomCentre(RoomGroup::Burst), roomCentre(RoomGroup::Paid), ramp(t, 0.2, 1.4));
    return k;
}
// 4. The map: the camera rises until the room reads as a plan, every mark showing.
Key rise(double t) {
    Key k;
    k.cam = mixCam(orbitCam(kBoardCentre, 16, 37, 27, 0.50), orbitCam(kBoardCentre, 6, 72, 36, 0.52), ramp(t, 0.3, 5.6));
    k.room.burstAt = -100; k.room.t = t; k.room.paidMark = 1; k.room.lookalikeMark = 1;
    k.focus = lerp(roomCentre(RoomGroup::Paid), kBoardCentre, ramp(t, 0.5, 3.0));
    return k;
}
const Shot kShots[] = {{"slot", 7.0, slot}, {"burst", 7.0, burst}, {"paid", 6.0, paid}, {"rise", 6.0, rise}};

View viewOf(const Cam& c, int w, int h) { View v; v.eye = c.eye; v.target = c.target; v.fovy = c.fovy; v.w = w; v.h = h; return v; }

// The screen position of a world point in `v`, as fractions of the frame; false behind the eye.
bool project(const View& v, const D3& p, double& sx, double& sy) {
    const auto sub = [](const D3& a, const D3& b) { return D3{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto dot = [](const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    D3 f = sub(v.target, v.eye);
    const double fl = std::sqrt(dot(f, f));
    for (double& x : f) x /= fl;
    D3 r{f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0};
    const double rl = std::sqrt(dot(r, r));
    for (double& x : r) x /= rl;
    const D3 u{r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const D3 q = sub(p, v.eye);
    const double z = dot(q, f);
    if (z <= 1e-6) return false;
    const double fp = v.focalPx();
    sx = (dot(q, r) * fp / z + 0.5 * v.w) / v.w;
    sy = (-dot(q, u) * fp / z + 0.5 * v.h) / v.h;
    return true;
}

std::string labelsLine(int frame, const View& v, const Key& k) {
    const std::pair<const char*, RoomGroup> groups[] = {{"poster", RoomGroup::Poster}, {"ordinary", RoomGroup::Ordinary}, {"paid", RoomGroup::Paid},
                                                        {"lookalike", RoomGroup::Lookalike}, {"burst", RoomGroup::Burst}};
    Json j;
    j.integer("frame", frame).num("t", k.room.t).num("paid_mark", k.room.paidMark).num("lookalike_mark", k.room.lookalikeMark);
    int landed = 0;
    for (const RoomFigure& f : roomFigures()) { D3 p; if (f.group == RoomGroup::Burst && roomFootAt(f, k.room, p) && p[1] < 0.05) ++landed; }
    j.integer("burst_landed", landed);
    for (const auto& [name, g] : groups) {
        double sx = -1, sy = -1;
        project(v, roomCentre(g), sx, sy);
        j.nums(name, {sx, sy});
    }
    return j.done();
}

int renderShot(const Options& o, const Shot& s, int fps) {
    const int w = o.width, h = o.height, F = std::max(1, int(std::lround(s.seconds * fps)));
    const std::string dir = o.out + "/" + s.name;
    char last[64];
    std::snprintf(last, sizeof last, "/f%04d.png", F - 1);
    if (std::filesystem::exists(dir + last)) { std::printf("art room: %s already rendered, skipped\n", s.name); return 0; }
    std::filesystem::create_directories(dir);
    std::ofstream labels(dir + "/labels.jsonl", std::ios::trunc);
    style::PaintParams pp = style::paintPreset("oil");
    pp.exposure = 1.35;   // the board is pale; the preset's exposure washes it out
    style::PaintPlan plan = style::resolvePaint(pp, w, h);
    plan.threads = o.threads;
    style::PaintCanvas canvas(w, h);
    const Lighting L = roomLight();
    const std::size_t n = std::size_t(w) * std::size_t(h);
    View prev;
    const auto t0 = std::chrono::steady_clock::now();
    double tTrace = 0, tPaint = 0, tDefocus = 0;
    auto lap = [](std::chrono::steady_clock::time_point& a) { const auto b = std::chrono::steady_clock::now(); const double d = std::chrono::duration<double>(b - a).count(); a = b; return d; };
    for (int i = 0; i < F; ++i) {
        const double t = double(i) / fps;
        const Key k = s.at(t);
        setRoomState(k.room);
        const View v = viewOf(k.cam, w, h);
        auto mark = std::chrono::steady_clock::now();
        const Frame f = renderFrame(v, L, o.threads, SceneKind::Room);
        tTrace += lap(mark);
        // The paint canvas rides the motion, so strokes stay on the figures as the camera moves.
        std::vector<float> mv = i == 0 ? std::vector<float>(n * 2, 0.0f) : motionTo(f, prev);
        std::vector<float> dist(n), distPrev(n);
        for (std::size_t p = 0; p < n; ++p) {
            dist[p] = f.dist[p] > 0 ? f.dist[p] : 1e4f;
            if (f.dist[p] <= 0 || i == 0) { distPrev[p] = dist[p]; continue; }
            const double dx = f.P[p * 3] - prev.eye[0], dy = f.P[p * 3 + 1] - prev.eye[1], dz = f.P[p * 3 + 2] - prev.eye[2];
            distPrev[p] = float(std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        style::paintDecide(canvas);
        style::paintAdvect(canvas, mv, dist, distPrev);
        const std::vector<float> paint = style::runPaint(plan, f.lit, &canvas);
        tPaint += lap(mark);
        const TiltLook look = tiltLook(k.look);
        const LensGeom g = resolveLens(lensOf(look, k.focus, {1, 0, 0}), v);
        const std::vector<float> def = paintDefocus(paint, cocMap(f, g), w, h, {}, DefocusMode::Paint, o.threads);
        const std::vector<float> img = fxStack(planFx(look, {i, false, false}, w, h, v.focalPx()), def);
        tDefocus += lap(mark);
        char name[64];
        std::snprintf(name, sizeof name, "/f%04d.png", i);
        if (!writeCodesPng(dir + name, w, h, codes8(img))) { std::printf("art room: cannot write %s%s\n", dir.c_str(), name); return 2; }
        labels << labelsLine(i, v, k) << "\n";
        labels.flush();
        prev = v;
        if (i % 12 == 0 || i == F - 1) {
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("art room: %s frame %d/%d, %.2f s a frame (trace %.2f, paint %.2f, defocus and effects %.2f)\n", s.name, i + 1, F, sec / (i + 1),
                        tTrace / (i + 1), tPaint / (i + 1), tDefocus / (i + 1));
            std::fflush(stdout);
        }
    }
    return 0;
}
}  // namespace

int room(const Options& o) {
    const int fps = std::max(1, o.fps);
    for (const Shot& s : kShots) {
        if (!o.shot.empty() && o.shot != "all" && o.shot != s.name) continue;
        if (const int rc = renderShot(o, s, fps)) return rc;
    }
    Json j;
    j.str("schema", "raw-native.art-room/1").integer("width", o.width).integer("height", o.height).integer("fps", fps).str("shot", o.shot.empty() ? "all" : o.shot)
     .integer("figures", long(roomFigures().size()));
    return writeText(o.out + "/room.json", j.done()) ? 0 : 2;
}

}  // namespace raw::art_cmd
