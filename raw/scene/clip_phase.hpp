#pragma once
// Phase-timed clips: the rest of web/anim/clip.mjs (layout, keyTime, phaseAt, warp) beside
// raw/scene/clip.hpp, in the same operation order, float64.
//
// A phased clip lays its keys on phases whose lengths come from data, so a designer retimes a move
// (startup, active, recovery frames) without touching a key. warp() maps a frame on one phase layout
// to the matching frame on another, piecewise linearly phase by phase, so a clip authored at one
// timing plays in step with gameplay frames at another: the art's wind-up ends on the move's last
// startup frame, whatever the two lengths are.
#include <string>
#include <utility>
#include <vector>
namespace raw::anim {

struct PhaseSpan { std::string phase; double from{0}, n{0}; };
// The frame layout of a phased clip: total length and each phase's [from, from + n), in phase order.
struct Layout {
    double length{0};
    std::vector<PhaseSpan> starts;
    const PhaseSpan* find(const std::string& phase) const;
};

// Phases in order with their lengths (frames). An empty list is an unphased clip of `frames`.
// Throws std::invalid_argument on a negative or missing length, or an unphased clip with no length.
Layout layout(const std::vector<std::pair<std::string, double>>& phases, double frames = 0);
// A key's frame: u within a phase, or u over the whole clip when phase is empty.
double keyTime(const std::string& phase, double u, const Layout& L);

struct PhaseAt { std::string phase; bool done{false}; };   // phase empty outside every phase
PhaseAt phaseAt(const Layout& L, double f);
// Map frame f on `from` to the matching time on `to`. Phases missing from either side are skipped;
// with none in common the map is proportional.
double warp(const Layout& from, const Layout& to, double f);

}  // namespace raw::anim
