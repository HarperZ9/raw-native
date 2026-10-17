#pragma once
// Private to src/tools/present/: the host's session (device, passes, swapchain, targets), its
// options, and the commands built on them.
#include "present_frames.hpp"
#include "raw/platform/window.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace raw::present {

enum Exit { kOk = 0, kCheckFailed = 1, kUsage = 2, kRuntime = 3, kSkip = 77 };

// Lifecycle events of a run, by frame index (evidence/n2-presentation-bounds.json, c2_lifecycle).
struct Schedule {
    bool on{false};
    uint32_t resizeA{200}, resizeB{400}, minimise{450}, lose{500};
    uint32_t altW{256}, altH{144};
    // The size frame f is drawn at, for a run whose base size is w x h.
    void sizeAt(uint32_t f, uint32_t w, uint32_t h, uint32_t& ow, uint32_t& oh) const {
        const bool alt = on && f >= resizeA && f < resizeB;
        ow = alt ? altW : w; oh = alt ? altH : h;
    }
};

struct RunOptions {
    uint32_t frames{600}, width{320}, height{180}, syncInterval{1};
    bool offscreen{false}, control{false}, readback{true}, hidden{false}, fullscreenCheck{false};
    Schedule schedule;
    std::string out, dump;    // dump: frame 0's read-back image as a binary PPM
};
struct FrameRecord { uint32_t frame, width, height; std::string hash; };
struct RunResult {
    std::vector<FrameRecord> frames;
    std::vector<std::string> events;   // what happened, in order, for the record
    std::string adapter, graph, error, backend, windowSystem, refreshSource{"none"};
    double refreshHz{0};
    uint32_t recoveries{0}, drawnWhileMinimised{0}, zeroResizes{0}, sizeMismatches{0};
    bool tearing{false}, fullscreenChecked{false}, fullscreenOk{false};
};

struct Session {
    std::unique_ptr<rhi::Device> dev;
    Pipelines pipes;
    Targets targets;
    std::unique_ptr<rhi::Swapchain> sc;
    std::string adapter;
};
// A new device with the passes and, given a window, a swapchain of `format` at its client size.
// `noDevice` is set when the failure is that no adapter exists (a skip, not an error).
bool openSession(Session& s, platform::Window* w, rhi::TextureFormat format, rhi::ColourSpace cs, uint32_t sync,
                 bool waitable, std::string& err, bool* noDevice = nullptr);
void closeSession(Session& s);
// Resize the swapchain and the targets together (the device is idle between frames).
bool resizeSession(Session& s, uint32_t w, uint32_t h, std::string& err);
// The output's refresh rate: the swapchain's when the backend knows it (DXGI's display
// configuration, VK_GOOGLE_display_timing), else the window's; `source` says which ("none": 0).
double outputRefreshHz(Session& s, const platform::Window& w, std::string& source);

int runPresented(const RunOptions& o, RunResult& r);
int runOffscreen(const RunOptions& o, RunResult& r);
std::string runJson(const RunOptions& o, const RunResult& r);
int hdrCommand(const std::string& out, bool quiet);
int pacingCommand(int argc, char** argv);
int selftest();

// JSON helpers.
std::string jstr(const std::string& s);
std::string jnum(double v);
bool writeText(const std::string& path, const std::string& text);
bool writePpm(const std::string& path, const FrameOut& f, uint32_t w, uint32_t h);

}  // namespace raw::present
