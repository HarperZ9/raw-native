#pragma once
// The post stack's driver in the C++ core (ADR 0019; evidence/cpp-port-style-bounds-2.json,
// post_stack). Full-screen passes a display list names in post.passes run in order around the
// finish: scene passes on the linear accumulation, display passes after the finish. This is the C++
// form of web/motion/post.mjs (the registry and the encoding contract), the scene and display split
// of web/motion/renderer.mjs, and the run order of web/motion/post_run.mjs (ping-pong frames and a
// history frame per history pass). A stage runs as one frame graph: on the host the passes are CPU
// references; on a device they are the WGSL passes of style.wgsl (src/renderer/gpu/post_stack_gpu).
//
// Encoding contract: the accumulation holds display values without post.colour and scene-linear
// light with it. A pass that reads linear light (input Linear) on display values gets srgb-decode
// inserted before it; a pass that writes display values (output Display) with post.colour set is
// refused (two tone mappers), and so is a linear-input pass after a display-output pass. The web
// contract changes the encoding only through a pass's input or output field, so a list that names
// srgb-decode itself (input Any) before a linear-input pass still gets a second decode; the plan
// keeps that behaviour, because the web plan is the oracle (reported in the round 2 runs file).
#include "raw/graph/frame_graph.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
namespace raw::post_stack {

enum class Stage : std::uint8_t { Scene, Display };
enum class Input : std::uint8_t { Any, Linear };
enum class Output : std::uint8_t { Linear, Display };
struct PassInfo {
    std::string name;
    Stage stage{Stage::Scene};
    bool history{false};
    Input input{Input::Any};
    Output output{Output::Linear};
};
// The passes the web registers, in its registration order (web/motion/post.mjs then
// web/shaders/post.mjs). A name here may have no C++ implementation yet.
const std::vector<PassInfo>& registry();
const PassInfo* find(const std::string& name);

// A pass in a list: its name and numeric parameters (exposure's ev, a palette size...).
struct Spec {
    std::string pass;
    std::vector<std::pair<std::string, double>> num;
    double get(const std::string& key, double otherwise) const;
};
struct Plan {
    bool refused{false};
    std::string why;
    std::vector<Spec> scene, display;   // scene: after srgb-decode insertion
};
// planScenePasses and the display filter of the web renderer. Unknown pass names are refused.
Plan plan(const std::vector<Spec>& specs, bool colourManaged);

// One stage's run order, as PostRunner.run builds it. Frames: kIn (the stage's input), kOut (the
// caller's target, only when toOutput), kPing0/kPing1, kHistNext, and the history frame of the
// pass at list index i, kHist0 + i.
enum Frame : int { kIn = 0, kOut = 1, kPing0 = 2, kPing1 = 3, kHistNext = 4, kHist0 = 5 };
struct Step {
    bool copy{false};          // a copy src -> dst (history forward, or the final blit)
    std::size_t spec{0};       // the pass at this list index (not a copy)
    int src{kIn}, prev{kIn}, dst{kOut};
};
struct Schedule { std::vector<Step> steps; int result{kIn}; };
// Throws std::invalid_argument when a pass of another stage is listed (as the web runner does).
Schedule schedule(const std::vector<Spec>& specs, Stage stage, bool toOutput);

// The host executor: frames are RGBA f32 rows (as the web cpu() references pass them), each
// pass computes in float64 and its output is stored as f32. History frames persist across run().
struct HostFrame { int w{0}, h{0}; std::vector<float> rgba; };
using CpuPass = std::function<void(const HostFrame& src, const HostFrame& prev, HostFrame& dst, const Spec& spec, int frame)>;
class HostStack {
public:
    // The CPU references of the passes with a C++ implementation (exposure, srgb-decode, ...).
    HostStack();
    void define(const std::string& name, CpuPass fn);
    bool has(const std::string& name) const;
    // Runs one stage's specs over `input` as a host frame graph; returns the stage's result.
    bool run(const std::vector<Spec>& specs, Stage stage, const HostFrame& input, int frame, HostFrame& out, std::string& err);
private:
    std::vector<std::pair<std::string, CpuPass>> passes_;
    std::vector<HostFrame> history_;
};

// The CPU references of the stack's built-in passes, float64 (web/motion/post.mjs).
double srgbDecode(double v);
void exposureCpu(const HostFrame& src, HostFrame& dst, double ev);
void srgbDecodeCpu(const HostFrame& src, HostFrame& dst);

}  // namespace raw::post_stack
