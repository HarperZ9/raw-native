#pragma once
// P23's measures and runs (evidence/tiltshift-bounds.json t1 to t6, d1 to d5, c1 to c3): ink weight
// in and out of the focal wedge, painted-not-blurred gradient statistics, the 05-CARE flash and red
// window counts, the orbit and rack temporal runs, and the director's scripted timeline.
#include "raw/renderer/art_metrics.hpp"
#include "raw/renderer/art_tiltshift.hpp"
#include "raw/renderer/focus_director.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::art {

// Display-linear luminance a pixel of RGBA f32.
std::vector<double> lumaOf(const std::vector<float>& rgba);
// The lit frame through the paint prep's shoulder at `exposure`, display-linear RGBA f32.
std::vector<float> displayOf(const std::vector<float>& lit, double exposure);
// Sum over mask pixels of max(0, Y(noLines) - Y(lines)).
double inkWeight(const std::vector<float>& lines, const std::vector<float>& noLines, const std::vector<std::uint8_t>& mask);
std::vector<std::uint8_t> dilate(const std::vector<std::uint8_t>& m, int w, int h, int r);
// D = mean |grad Y_out| / mean |grad Y_in| and E = P95(|grad Y_out|) / P95(|grad Y_in|) over mask.
struct GradStats { double D{0}, E{0}; long n{0}; };
GradStats gradStats(const std::vector<float>& out, const std::vector<float>& in, const std::vector<std::uint8_t>& mask, int w, int h);

// 05-CARE rule 1 on 1/16-frame windows sliding by half a window: for each window and each run of
// `fps` frames, the opposing transitions of at least 10% of the brighter with the darker below
// 0.80, halved to flashes. Returns the largest count over windows and runs.
int maxFlashes(const std::vector<std::vector<float>>& frames, int w, int h, int fps);
// 05-CARE rule 4: windows whose mean linear colour is saturated red.
int redWindows(const std::vector<float>& img, int w, int h);
long redPixels(const std::vector<float>& img);

// t5 and t6: the base paint frames against the lit reference, and the tilt-shifted frames against
// the lit frame defocused optically by the same CoC.
struct TemporalRun { std::vector<double> ours, base; Paired oursVsBase; };
TemporalRun runTiltOrbit(int w, int h, int frames, int first, int threads, std::uint64_t seed, bool rack, double damping = 1.0);

// d1 to d5: the director over a scripted timeline on the town.
struct DirectorReport {
    double maxOvershoot{0}, controlOvershoot{0};
    int lockFails{0}, lockFramesChecked{0}, controlLockFails{0}, eventsInLocks{0};
    double idleInLocks{0};
    int drifts{0}, calmDrifts{0}, calmAnimated{0};
    std::vector<double> dwells, returns;
    double eventLatency{-1}, eventSettle{-1};
    double maxJitterStep{0}, jitterHz{0}, calmJitter{0};
    std::string notes;
};
DirectorReport runDirectorChecks();

}  // namespace raw::art
