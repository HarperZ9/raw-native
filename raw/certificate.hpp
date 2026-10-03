#pragma once
#include <string>
#include <vector>
#include <utility>
#include <optional>
namespace raw {
// The shared witnessed form. JSON shape is byte-shape-compatible with
// coherence-membrane's Certificate.to_dict(): {claim, verdict, oracle, evidence}.
// When `channels` is present, an additional "channels" object is appended after
// "evidence"; when absent the JSON is byte-identical to the original shape.
enum class Verdict { Verified, Refuted, Unverifiable };
const char* verdict_str(Verdict v);   // "verified" | "refuted" | "unverifiable"

// Per-channel fidelity record: how trustworthy each extra channel
// is, witnessed honestly. A channel with no data carries std::nullopt and is
// emitted as JSON null, never a faked pass.
//   - aoFidelity:     AO reconcile confidence in 0..1 (1 == exact, derived from
//                     the SSAO-vs-RTAO rmse).
//   - motionCoherence:fraction of covered pixels with a valid finite motion
//                     vector, in 0..1.
//   - hdrHeadroom:    max linear radiance over the frame (>= 0; > 1 means the
//                     human's 8-bit frame is clipping information the model keeps).
struct ChannelFidelity {
    std::optional<double> aoFidelity;
    std::optional<double> motionCoherence;
    std::optional<double> hdrHeadroom;
};

struct Certificate {
    std::string claim;
    Verdict verdict;
    std::string oracle;                                       // e.g. "raw-rt-ao-v1"
    std::vector<std::pair<std::string, std::string>> evidence; // ordered (key,value) pairs
    std::optional<ChannelFidelity> channels;                  // absent -> original JSON shape
};
std::string to_json(const Certificate& c);
struct ReconcileResult;   // fwd (defined in raw/reconcile.hpp)
struct ArenaStats;        // fwd (defined in raw/arena.hpp)
Certificate certificate_from_reconcile(const ReconcileResult& r, float tolerance);
Certificate certificate_from_arena(const ArenaStats& s);

// Map a reconcile RMSE to an AO fidelity confidence in 0..1 (1 == zero error).
// Honest: clamps to [0,1] and saturates rather than reporting a confidence > 1.
double aoFidelityFromRmse(double rmse);

// Build the AO reconcile certificate AND attach the per-channel fidelity block.
// Existing reconcile claim/verdict/oracle/evidence are preserved exactly; the
// channel block is added. Any std::nullopt argument is witnessed as null.
Certificate certificate_with_channels(const ReconcileResult& r, float tolerance,
                                      std::optional<double> motionCoherence,
                                      std::optional<double> hdrHeadroom);
}
