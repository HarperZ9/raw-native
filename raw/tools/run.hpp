#pragma once
#include "raw/renderer/render.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/cert/certificate.hpp"
#include "raw/core/arena.hpp"
#include <string>
#include <utility>
#include <vector>
namespace raw {
using FileDigests = std::vector<std::pair<std::string, std::string>>;   // name -> sha256, sorted
// Render one frame for the CLI params: the built-in scene with the requested
// camera, previous camera for motion, tolerance, RT on/off and thread count.
FrameResult renderFromParams(const CliParams& p, Arena* arena);
// Write every image file for a rendered frame into p.out and return the SHA-256
// of each file as written, sorted by name. With --no-rt the ray-traced files
// are not written, because there is no reference to write.
FileDigests writeFrameFiles(const FrameResult& o, const CliParams& p);
// The AO certificate in raw-cert/2 form: the 0.2.0 fields, the channel block,
// and the provenance block (renderer, params, samples, exact values, outputs).
Certificate aoCertificate(const FrameResult& o, const CliParams& p, const FileDigests& outputs);
// Time p.bench renders (no files written) and return a JSON record with every
// run in milliseconds and the median. Each run is a full render at p's size.
std::string benchJson(const CliParams& p);
// Level-1 recheck of an output directory with no rendering: re-hash every file
// listed in certificate.json, recompute the reconcile from ao_rt.pfm, ao_ss.pfm
// and mask.pgm, and compare with the recorded exact values and verdict.
// Returns 0 when everything matches, 3 on any mismatch, 2 when files are missing
// or unreadable. `report` receives one line per check.
int verifyDir(const std::string& dir, std::string& report);
}
