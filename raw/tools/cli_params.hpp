#pragma once
#include "raw/math/vec.hpp"
#include "raw/scene/scene.hpp"
#include <string>
#include <optional>
namespace raw {
// Parsed render parameters for the parameterizable CLI: the model picks a view,
// a frame size, and (optionally) a previous camera for motion vectors, then
// reads the channels back. Everything has a sane default so a bare invocation
// renders the canonical test view.
//
// The two-way loop sets these per re-render: perceive channels -> steer the
// camera/fov -> re-render -> perceive again.
struct CliParams {
    std::string out{"."};        // output directory
    int width{256}, height{256}; // frame size
    Vec3 eye{4,4,6};             // camera eye (world)
    Vec3 center{0,1,0};          // camera look-at target (world)
    Vec3 up{0,1,0};              // camera up
    float fovy{0.9f};            // vertical field of view, radians
    float tolerance{0.12f};      // RMSE bound for the AO verdict
    bool  rtao{true};            // false: skip the ray-traced reference (--no-rt)
    int   threads{1};            // render threads; output is identical for any count
    int   bench{0};              // > 0: time this many renders, print JSON, write no files
    bool  gpu{false};            // render on the GPU backend and reconcile it against the CPU
    std::string gpuBackend;      // the backend's name for the certificate ("webgpu", "d3d12", "none")
    // A glTF model (--model) in place of the built-in box, set on the same ground plane
    // (raw/tools/model_scene.hpp). The certificate records its file name and SHA-256.
    std::string model;
    std::string modelSha256;     // filled in when --model is parsed
    // Previous camera for motion reprojection. Absent -> static frame (the
    // previous view equals the current view, so motion is honestly all-zero).
    std::optional<Vec3> prevEye;
    std::optional<Vec3> prevCenter;
    std::optional<Vec3> prevUp;
    bool hasPrevCamera() const {
        return prevEye.has_value() || prevCenter.has_value() || prevUp.has_value();
    }
};

// Build a Camera from the params (aspect derived from width/height). The arena
// is unused here; the camera is a plain value.
Camera cameraFromParams(const CliParams& p);

// Build the previous-frame camera used for motion reprojection. Any previous
// field the caller omitted falls back to the corresponding current field, so a
// partial previous camera is still well-defined.
Camera prevCameraFromParams(const CliParams& p);

// Parse argv into CliParams. Recognized flags (all optional):
//   --out <dir>                     (positional arg 1 also accepted, back-compat)
//   --width <int>  --height <int>
//   --eye x,y,z    --target x,y,z   --up x,y,z
//   --fovy <radians>
//   --tolerance <rmse>  --no-rt  --threads <n>  --bench <runs>  --gpu
//   --prev-eye x,y,z  --prev-target x,y,z  --prev-up x,y,z
//   --params <file.json>            (loaded first; later flags override it)
// On a malformed value, returns std::nullopt and writes a reason to `err`.
// Canonical JSON of every parameter that changes the rendered output: sorted
// keys, floats at round-trip precision, absent previous camera as null. `out`,
// `threads` and `bench` are excluded because they do not change the pixels.
// A GPU render adds "backend":"<gpuBackend>" ("none" when unset); a CPU
// render's JSON is unchanged.
std::string canonicalParamsJson(const CliParams& p);

std::optional<CliParams> parseArgs(int argc, const char* const* argv, std::string& err);

// Parse a small flat JSON params object (stdlib only, no third-party parser):
//   {"out":"...","width":256,"height":256,
//    "eye":[x,y,z],"target":[x,y,z],"up":[x,y,z],"fovy":0.9,
//    "prev_eye":[x,y,z],"prev_target":[x,y,z],"prev_up":[x,y,z]}
// Only the keys present are applied onto `p`. Returns false + a reason on
// malformed JSON. Unknown keys are ignored (honest, forward-compatible).
bool applyParamsJson(const std::string& json, CliParams& p, std::string& err);

// Load a params JSON file from disk and apply it onto `p`. Returns false + a
// reason if the file cannot be read or the JSON is malformed.
bool loadParamsFile(const std::string& path, CliParams& p, std::string& err);
}
