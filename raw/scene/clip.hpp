#pragma once
// Keyed channel clips, a port of web/anim/clip.mjs monotoneCubic and sampleClip (looping) in the
// same operation order, float64. Keys sit at times in the clip's own units (seconds here); every
// channel is interpolated with a monotone cubic (Fritsch and Carlson 1980), so a held pose stays
// held and motion never overshoots a key. A looping clip wraps the time and closes on its first key.
// Sampled at a held time (raw/scene/held_time.hpp) it steps on ones, twos or threes.
#include <vector>
namespace raw::anim {

struct ClipKey { double t{0}; std::vector<double> v; };   // every key carries every channel
struct Clip { double length{1}; bool loop{true}; std::vector<ClipKey> keys; };

double monotoneCubic(const std::vector<double>& ts, const std::vector<double>& vs, double t);
// Channel values at time t. Keys must be sorted by t and share a channel count.
std::vector<double> sampleClip(const Clip& clip, double t);

}  // namespace raw::anim
