#pragma once
// The impact preview bench (G6): replays the contacts of a contact trace through the one schedule
// (raw/game/action/schedule.hpp) and draws them frame by frame (raw/renderer/impact.hpp), with the sound on the
// same ticks as a WAV.
//   raw_native_cli action-impact <trace.jsonl> <tables.json> <outdir> [--from T] [--frames N] [--w W] [--h H]
//                                [--contact I] [--around N] [--loop W] [--metres M] [--no-overlays] [--sound-late N]
// --contact I centres the window on the I-th contact (N frames before it); --metres M frames M metres of ground
// across (default 4.2); --loop W also writes a GIF and a
// lossless WebP at width W through raw-native's own loop writers. Writes frame-NNNN.png, contact.wav and run.json.
// Exit 0 done, 2 bad input.
#include "raw/game/action/schedule.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace raw {

struct ImpactView {
    int w = 1920, h = 1080, from = 1, frames = 60;
    double metres = 4.2;          // the width of ground the frame shows
    bool overlays = true;         // the live hit volume as a thin outline
};

struct ImpactFrames {
    int w = 0, h = 0;
    std::vector<std::vector<std::uint8_t>> rgb;   // one RGB8 frame a tick, from view.from
    std::vector<int> trail_px, burst_px;           // per frame: pixels the trail layer and the burst layer covered
    std::vector<double> shake_px;                  // per frame: the camera's offset length
    std::vector<int> burst_first;                  // per contact (schedule order): the first frame its burst shows, -1 if none
};

ImpactFrames render_impact(const game::action::Schedule& s, const ImpactView& view);
// The sound track: each Sound cue's voice starting at its tick (48 kHz mono). contact >= 0: that contact alone.
std::vector<float> impact_audio(const game::action::Schedule& s, int from, int frames, int rate = 48000, int contact = -1);
bool write_wav(const std::string& path, const std::vector<float>& samples, int rate);
// The first sample at or after `start` whose magnitude exceeds `level`; -1 when none.
long first_onset(const std::vector<float>& samples, long start, double level);

int actionImpactCommand(int argc, char** argv);

}  // namespace raw
