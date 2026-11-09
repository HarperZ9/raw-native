// raw/game/action/markers: a move's timing read from markers on its animation clip (G3).
//
// A marker clip names frames on a 60 Hz clip: `active` (the first active frame), `recovery` (the
// first recovery frame), `end` (the free frame), the tell window and each hit window [on, off]
// (inclusive). Startup, active and recovery are read from the markers and laid out as raw::anim
// phases (raw/scene/clip_phase.hpp), so an art clip authored at its own timing warps onto them.
// Each hit carries the game's ring (arc in degrees from facing, radii, height band) that
// raw/game/action/hitvol turns into a capsule bound to a bone.
//
// check_markers compares a clip with the move table: a clip and a table that disagree are two
// sources that drifted, and the exporter test fails on it. Layer: game (reads scene).
#pragma once
#include "raw/game/action/moves.hpp"
#include "raw/scene/clip_phase.hpp"
#include <string>
#include <vector>

namespace raw::game::action {

struct HitMarker {
    int on = 0, off = -1;                // live frames, inclusive
    double arc[2] = {0, 0}, r[2] = {0, 0}, y[2] = {0, 2.2};
};

struct MarkerClip {
    std::string key;                     // "<set> <move id>"
    int active = 0, recovery = 0, end = 0;
    bool has_tell = false; int tell_on = 0, tell_off = -1;
    std::string tell_kind;               // swing, slam, grab, sweep, pounce, thrust, throw, shout
    std::vector<HitMarker> hits;
    bool parryable = true, blockable = true, signature = false;
    int startup() const { return active; }
    // startup, active, recovery phases laid out from the markers
    raw::anim::Layout phases() const;
};

struct MarkerSet {
    int fps = 60;
    std::string commit;                  // the source the markers were exported from
    std::vector<MarkerClip> clips;
    const MarkerClip* find(const std::string& key) const;
};

// raw-clip-markers v1 (tools/action/export_markers.mjs). Throws std::runtime_error on a wrong format,
// version or fps, or a clip whose markers run backwards.
MarkerSet load_markers(const std::string& text);
// One line a disagreement: "<key> <marker> clip=<a> move=<b>". Empty when they agree.
std::vector<std::string> check_markers(const MarkerClip& c, const Move& m);

}  // namespace raw::game::action
