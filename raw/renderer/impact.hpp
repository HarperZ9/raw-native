// raw/renderer/impact: the CPU drawing of a blow's presentation (G6), game-agnostic. A caller that schedules
// cues on the sim tick (raw/game/action/schedule.hpp) draws them here frame by frame:
//   ShakeCam   the impact camera of Low Water's ui/fightcam.mjs "shake" treatment, ported: a keyed shake
//              (a jump to the peak at frame 3, a 40% swing back, still by 12, 15 or 40 frames) plus an inertial
//              spring the blow kicks (k 300, c 30, stepped at 100 Hz), one vector limit on the sum: 6 px at 1080p,
//              3 px while a telegraph shows. A full-weight blow peaks at 6 px.
//   Canvas     an RGB8 frame with alpha-blended, anti-aliased primitives
//   draw_*     the trail ribbon, the smear wedge, the flat sawtooth burst, the hitmarker, a capsule and a proxy body
// Colours are linear-free sRGB bytes; nothing here reads a game type. Layer: renderer.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace raw::impact {

struct Offset { double dx = 0, dy = 0; };

class ShakeCam {
public:
    double view_h = 1080;       // the frame's height: offsets scale with it (h / 1080)
    double kick = 2.2;          // the spring's kick a blow gives (planted checks change it)
    bool tell_cap = true;       // hold the whole offset to 3 px while a tell shows (planted checks turn it off)
    double key_stretch = 1;     // the keyed curve's time scale (planted checks stretch it)
    // amp 0..1 (combat's shake amp), a catch keys the longer shake, (dx, dy) the screen direction of the blow
    void blow(double amp, bool is_catch, double dx, double dy);
    void tells(bool showing) { tell_ = showing; }
    Offset tick(double dt);
private:
    struct Keyed { double f = 0, amp = 0, d[2] = {0, 0}; int keys = 0; };
    bool has_ = false, tell_ = false;
    Keyed shake_;
    double x_[2] = {0, 0}, v_[2] = {0, 0}, acc_ = 0;
};
// fightcam.mjs keyed(): the key curve's value at frame f (smoothstep between keys); false once past the last key.
bool keyed(int keys, double f, double& out);

using Rgb = std::array<std::uint8_t, 3>;

struct Canvas {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgb;
    Canvas(int W, int H, Rgb ground);
    void blend(int x, int y, Rgb c, double a);
};

// A thick segment with round caps, anti-aliased over one pixel.
void draw_segment(Canvas& cv, double x0, double y0, double x1, double y1, double half_width, Rgb c, double alpha);
void draw_disc(Canvas& cv, double x, double y, double r, Rgb c, double alpha);
// A filled polygon (even-odd), anti-aliased at its edges by 4 x 4 coverage samples a pixel.
void fill_polygon(Canvas& cv, const std::vector<std::array<double, 2>>& pts, Rgb c, double alpha);

// The trail: one ribbon segment from (x0, y0) to (x1, y1), its width and alpha fading with age (0 newest).
void draw_ribbon(Canvas& cv, double x0, double y0, double x1, double y1, int age, int life, Rgb c);
// The smear: an annular wedge about (cx, cy) between angles a0 and a1 (radians, screen space), radii r0..r1.
void draw_smear(Canvas& cv, double cx, double cy, double a0, double a1, double r0, double r1, Rgb c, double alpha);
// The burst: a flat sawtooth star (reference 11), `teeth` spikes, its age 0..life scaling it out and away.
void draw_burst(Canvas& cv, double cx, double cy, double radius, int teeth, double spin, int age, int life, Rgb c);
// The hitmarker: four short strokes on the diagonals, opening as it ages.
void draw_hitmarker(Canvas& cv, double cx, double cy, double size, int age, int life, Rgb c);

}  // namespace raw::impact
