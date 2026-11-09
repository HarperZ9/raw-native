// raw_native_cli action-dump tells | trials (raw/tools/action_cmd.hpp): the telegraph retime lint over
// a clip-marker file, and the blind readability test's trial and key files (G5).
#include "raw/tools/action_cmd.hpp"
#include "raw/game/action/retime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace raw {
namespace {

using namespace raw::game::action;
using raw::story::Json;

std::string read(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("cannot read ") + path);
    std::stringstream s; s << f.rdbuf(); return s.str();
}
const char* opt(int argc, char** argv, const char* name, const char* d) {
    for (int i = 0; i + 1 < argc; ++i) if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return d;
}
TellFit fit_of(int argc, char** argv) {
    TellFit f; f.windup = std::atof(opt(argc, argv, "--windup", "0.5")); f.strike = std::atof(opt(argc, argv, "--strike", "1.5"));
    const std::string e = opt(argc, argv, "--ease", "linear");
    f.ease = e == "in" ? Ease::In : e == "out" ? Ease::Out : e == "inout" ? Ease::InOut : Ease::Linear;
    return f;
}

int tells(int argc, char** argv) {
    const MarkerSet ms = load_markers(read(argv[2]));
    const TellFit fit = fit_of(argc, argv);
    int n = 0, bad = 0;
    std::printf("%-40s need flat lead regions\n", "move");
    for (const MarkerClip& c : ms.clips) {
        if (!c.has_tell) continue;
        const Retime r = fit_telegraph(c.startup(), c.tell_on, fit);
        const RetimeReport rep = lint_retime(c, r);
        std::string regs;
        for (const TimeScale& s : r.regions) { char b[64]; std::snprintf(b, sizeof b, " %.3gx[%d,%d)", s.value, s.start, s.end); regs += b; }
        std::printf("%-40s %4d %4d %4d%s\n", c.key.c_str(), rep.need, rep.lead_flat, rep.lead, regs.c_str());
        for (const std::string& l : rep.issues) std::printf("  ISSUE %s\n", l.c_str());
        ++n; bad += !rep.issues.empty();
    }
    std::printf("%d tell moves, %d with issues\n", n, bad);
    return bad ? 1 : 0;
}

// The proxy figure's pose for a tell kind: arm, other arm, elbow (degrees from hanging down, forward
// positive), torso lean (degrees) and crouch (0..1). A stand-in for the game's art, not the art.
struct Pose { double arm, arm2, elbow, lean, crouch; };
Pose key_pose(const std::string& kind, bool strike) {
    static const std::map<std::string, std::pair<Pose, Pose>> K = {
        {"swing", {{210, 20, 40, -10, 0.1}, {60, 20, 0, 15, 0.2}}},
        {"slam", {{170, 170, 20, -15, 0}, {20, 20, 0, 30, 0.5}}},
        {"grab", {{70, 70, 50, 5, 0.3}, {95, 95, 0, 35, 0.3}}},
        {"sweep", {{-70, 10, 10, -5, 0.6}, {110, 10, 0, 10, 0.6}}},
        {"pounce", {{30, 30, 20, -20, 0.8}, {100, 100, 0, 45, 0}}},
        {"thrust", {{60, 20, 120, -10, 0.2}, {90, 20, 0, 20, 0.2}}},
        {"throw", {{200, 30, 90, -10, 0}, {120, 30, 0, 10, 0}}},
        {"shout", {{30, 30, 30, -20, 0}, {140, 140, 0, 0, 0}}}};
    auto it = K.find(kind);
    const auto& p = it == K.end() ? K.at("swing") : it->second;
    return strike ? p.second : p.first;
}
Pose mix(const Pose& a, const Pose& b, double t) {
    return {a.arm + (b.arm - a.arm) * t, a.arm2 + (b.arm2 - a.arm2) * t, a.elbow + (b.elbow - a.elbow) * t,
            a.lean + (b.lean - a.lean) * t, a.crouch + (b.crouch - a.crouch) * t};
}
// Rest to the wind-up pose over the first 70% of startup (smoothstep), held, then to the strike pose
// over the active frames. Driven by the clip frame only: flat and retimed differ in timing alone.
Pose pose_at(const MarkerClip& c, double clip) {
    const Pose rest{10, 10, 10, 0, 0}, w = key_pose(c.tell_kind, false), s = key_pose(c.tell_kind, true);
    const double st = std::max(1, c.startup());
    if (clip < st) { const double u = std::clamp(clip / (st * 0.7), 0.0, 1.0); return mix(rest, w, u * u * (3 - 2 * u)); }
    const double v = std::clamp((clip - st) / std::max(1, c.recovery - c.active), 0.0, 1.0);
    return mix(w, s, 1 - (1 - v) * (1 - v));
}

// Poses for sim frames 0..startup: the trial stops on the first active frame.
Json frames_of(const MarkerClip& c, const Retime& r) {
    Json a = Json::arr();
    for (int f = 0; f <= c.startup(); ++f) {
        const Pose p = pose_at(c, clip_time(r, f));
        a.add(Json::arr().add(num(p.arm)).add(num(p.arm2)).add(num(p.elbow)).add(num(p.lean)).add(num(p.crouch)));
    }
    return a;
}

uint32_t next(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

// Parryable tells with a hit volume, at most `per` a kind, in a seeded order.
std::vector<const MarkerClip*> pick(const MarkerSet& ms, int per, uint32_t& seed) {
    std::vector<const MarkerClip*> pool, out;
    for (const MarkerClip& c : ms.clips) if (c.has_tell && c.parryable && !c.hits.empty()) pool.push_back(&c);
    for (size_t i = pool.size(); i > 1; --i) std::swap(pool[i - 1], pool[next(seed) % i]);
    std::map<std::string, int> taken;
    for (const MarkerClip* c : pool) if (taken[c->tell_kind]++ < per) out.push_back(c);
    return out;
}

void write_file(const char* path, const Json& j) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("cannot write ") + path);
    f << raw::story::dump_json(j) << "\n";
}

int trials(int argc, char** argv) {
    const MarkerSet ms = load_markers(read(argv[2]));
    uint32_t seed = (uint32_t)std::atoi(opt(argc, argv, "--seed", "1"));
    const std::vector<const MarkerClip*> chosen = pick(ms, std::atoi(opt(argc, argv, "--per-kind", "6")), seed);
    struct T { const MarkerClip* c; bool retimed; };
    std::vector<T> order;
    for (const MarkerClip* c : chosen) { order.push_back({c, false}); order.push_back({c, true}); }
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[next(seed) % i]);
    Json opts = Json::arr(), legend = Json::obj(); std::map<std::string, int> kinds;
    for (const MarkerClip* c : chosen) if (!kinds[c->tell_kind]++) {
        opts.add(Json::str(c->tell_kind));
        Json poses = Json::arr();
        for (bool strike : {false, true}) { const Pose p = key_pose(c->tell_kind, strike); poses.add(Json::arr().add(num(p.arm)).add(num(p.arm2)).add(num(p.elbow)).add(num(p.lean)).add(num(p.crouch))); }
        legend.set(c->tell_kind, poses);
    }
    Json tr = Json::arr(), key = Json::arr();
    for (size_t i = 0; i < order.size(); ++i) {
        const MarkerClip& c = *order[i].c;
        const Retime r = order[i].retimed ? fit_telegraph(c.startup(), c.tell_on, fit_of(argc, argv)) : Retime{};
        const std::string id = "t" + std::to_string(i + 1);
        tr.add(Json::obj().set("id", Json::str(id)).set("frames", frames_of(c, r)));
        key.add(Json::obj().set("id", Json::str(id)).set("move", Json::str(c.key)).set("answer", Json::str(c.tell_kind))
                    .set("condition", Json::str(order[i].retimed ? "retimed" : "flat")));
    }
    write_file(argv[3], Json::obj().set("format", Json::str("raw-tell-trials")).set("version", num(1)).set("fps", num(FPS))
                            .set("figurePx", num(134)).set("options", opts).set("legend", legend).set("trials", tr));
    write_file(argv[4], Json::obj().set("format", Json::str("raw-tell-key")).set("version", num(1)).set("key", key));
    std::printf("%zu moves, %zu trials, %zu answer options\n", chosen.size(), order.size(), opts.a.size());
    return 0;
}

}  // namespace

int actionTellsCommand(int argc, char** argv) {
    try {
        const std::string sub = argc >= 2 ? argv[1] : "";
        if (sub == "tells" && argc >= 3) return tells(argc, argv);
        if (sub == "trials" && argc >= 5) return trials(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "action-dump: %s\n", ex.what()); return 2;
    }
    std::fprintf(stderr, "usage: action-dump tells <clip-markers.json> [--windup x --strike y --ease e] | "
                         "trials <clip-markers.json> <trials.json> <key.json> [--seed n --per-kind n]\n");
    return 2;
}

}  // namespace raw
