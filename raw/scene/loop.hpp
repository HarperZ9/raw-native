#pragma once
// The closed-loop contract (PROPOSALS P2 of the 2026-10-29 game-art study;
// evidence/heldtime-loop-bounds.json). A loop is N frames at a rational fps, and frame N must equal
// frame 0. Every animated term (weather, water, lights, held-time seeds, camera) declares its period
// in frames; a period that does not divide N is refused, as is a held layer whose ticks per loop are
// not whole. Terms read the raw frame index and reduce it by their own period (phase()), so a term
// that is not periodic shows up in the measured frame N against frame 0 check, not only here.
// The idea, periods that divide the loop, comes from superframe's Seattle scenes; no file of it.
#include "raw/scene/held_time.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::anim {

struct LoopSpec { std::int64_t frames{96}; Rational fps{24, 1}; };
struct LoopTerm { std::string name; std::int64_t period{0}; };   // period in frames; 0 = static

// A held layer as a term: its period in frames is the loop when its ticks a loop are whole and its
// clip (clipTicks ticks long, 0 = no clip) divides them; otherwise period 0 with ok false.
struct LayerTerm { LoopTerm term; bool ok{false}; std::string why; };
LayerTerm layerTerm(const Layer& layer, const LoopSpec& spec, std::int64_t clipTicks);

struct LoopCheck { bool ok{true}; std::vector<std::string> refused; };
LoopCheck checkLoop(const LoopSpec& spec, const std::vector<LoopTerm>& terms);

// The phase of frame k in a term of `period` frames: (k mod period) / period, exact for integer k.
double phase(std::int64_t frame, std::int64_t period);
// Ticks a loop for a held layer, or -1 when they are not whole.
std::int64_t ticksPerLoop(const Rate& rate, const LoopSpec& spec);

}  // namespace raw::anim
