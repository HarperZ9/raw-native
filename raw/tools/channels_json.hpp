#pragma once
#include "raw/renderer/render.hpp"
#include "raw/tools/cli_params.hpp"
#include <string>
namespace raw {
// The single machine-readable artifact the two-way loop reads back. It carries
// the per-channel fidelity CERTIFICATE plus compact SUMMARIES of every channel a
// model needs to reason, so a caller never has to parse the raw PPM/PFM/PGM
// files. Honest by construction: a channel with no data is emitted as JSON null,
// never a fabricated value.
//
// Schema (stable keys; absent data -> null):
// {
//   "schema": "raw-channels/1",
//   "frame": { "width":W, "height":H,
//              "files": {"ppm":"frame.ppm","pfm":"frame_hdr.pfm",
//                        "ao_rt":"ao_rt.pgm","ao_ss":"ao_ss.pgm","ao_error":"ao_error.pgm"} },
//   "camera": { "eye":[x,y,z], "target":[x,y,z], "up":[x,y,z], "fovy":f, "aspect":a,
//               "prev": {"eye":[..],"target":[..],"up":[..]} | null },
//   "certificate": { ...certificate_with_channels(...) JSON... },
//   "channels": {
//     "coverage": <fraction 0..1>,
//     "depth":  { "min":..,"max":..,"mean":.. } | null,   // over covered pixels
//     "normal": { "mean":[x,y,z] } | null,                // mean covered normal
//     "motion": { "valid":n,"total":n,"coherence":0..1|null,"max_magnitude":..,"mean_magnitude":.. },
//     "hdr":    { "headroom":maxRadiance, "clipping_fraction":0..1 },
//     "ao":     { "rmse":..,"max_error":..,"fidelity":0..1|null,"within_tolerance":bool },
//     "readout": { "kind":"luminance","width":n,"height":n,"rows":[[..],..] } // coarse downsample 0..1
//   }
// }
std::string channelsJson(const FrameResult& o, const CliParams& p,
                         int readoutW = 8, int readoutH = 8);
}
