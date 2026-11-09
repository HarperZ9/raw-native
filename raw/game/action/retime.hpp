// raw/game/action/retime: telegraph retiming as data (G5; SPECS F4).
//
// Per-action time-scale regions {value, start, end, ease} on sim frames [start, end) drive the
// presentation clock: across a region the clip advances value * (end - start) clip frames, shaped by
// the ease; outside every region it advances one clip frame a sim frame. The sim keeps its integer
// frames and never reads this. A telegraph reads as a held pose and then a sharp strike: the wind-up
// plays slow (0.25x to 0.75x), the strike fast (1.2x to 2.0x), landing on the first active frame.
//
// Hit volumes are keyed to the retimed clip: a hit whose clip markers are [on, off] is live on the
// sim frames whose clip time reaches on and stays before off + 1 (sim_frame_at). A retime that does
// not land the clip on the move's frames moves the hit volumes, and lint_retime reports it.
// Layer: game.
#pragma once
#include "raw/game/action/markers.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace raw::game::action {

enum class Ease : uint8_t { Linear, In, Out, InOut };
const char* ease_name(Ease e);
double ease_curve(Ease e, double u);     // 0 at 0, 1 at 1, non-decreasing

struct TimeScale { double value = 1; int start = 0, end = 0; Ease ease = Ease::Linear; };
struct Retime { std::vector<TimeScale> regions; };   // sorted, not overlapping

// The clip frame shown at sim frame f (f may be fractional; frames before 0 map to themselves).
double clip_time(const Retime& r, double f);
// The first sim frame in [0, limit] whose clip time reaches clip_frame; limit + 1 when none does.
int sim_frame_at(const Retime& r, double clip_frame, int limit);

// The authoring helper: a wind-up region from the tell onset at `windup`, a strike region to the
// first active frame, split on an integer frame, the strike value solved so the clip lands on startup.
struct TellFit { double windup = 0.5, strike = 1.5; Ease ease = Ease::Linear; };
Retime fit_telegraph(int startup, int tell_on, const TellFit& fit = {});

// T9's need (Low Water combat/data/innermoves.mjs needOf): 36 for a signature, 30 unparryable or
// unblockable, else 18 frames from tell onset to the first active frame.
int lead_need(const MarkerClip& c);

struct LintBounds { double windup_lo = 0.25, windup_hi = 0.75, strike_lo = 1.2, strike_hi = 2.0; };
struct RetimeReport {
    int need = 0, lead_flat = 0, lead = 0;   // lead: retimed first active frame minus retimed tell onset
    std::vector<std::string> issues;         // "<key> <check>: <detail>"
};
// Checks one tell move's retime: lead (every tell), keyed hit frames (every hit), the clip landing on
// the first active frame, and the slow-then-fast shape (parryable tells only).
RetimeReport lint_retime(const MarkerClip& c, const Retime& r, const LintBounds& b = {});

}  // namespace raw::game::action
