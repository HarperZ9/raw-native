#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "raw/arena.hpp"
#include <cstdio>
#include <string>
#include <cmath>
#include <optional>
namespace raw {
static std::string f4(double v){ char b[64]; std::snprintf(b, sizeof b, "%.4f", v); return b; }
double aoFidelityFromRmse(double rmse){
    // Map AO reconcile error to a confidence in 0..1: 1 at zero error, decaying
    // as 1/(1+rmse). Monotonic, saturating, never reports above 1 or below 0.
    if (!std::isfinite(rmse) || rmse < 0.0) return 0.0;
    double c = 1.0 / (1.0 + rmse);
    if (c < 0.0) c = 0.0;
    if (c > 1.0) c = 1.0;
    return c;
}
Certificate certificate_from_reconcile(const ReconcileResult& r, float tolerance){
    Verdict v = (r.pixels == 0) ? Verdict::Unverifiable
              : (r.withinTolerance ? Verdict::Verified : Verdict::Refuted);
    return Certificate{
        "screen-space AO matches ray-traced ground truth within tolerance",
        v, "raw-rt-ao-v1",
        { {"pixels", std::to_string(r.pixels)},
          {"rmse", f4(r.rmse)},
          {"maxError", f4(r.maxError)},
          {"tolerance", f4(tolerance)} }
    };
}
Certificate certificate_from_arena(const ArenaStats& s){
    Verdict v = (s.refusals == 0) ? Verdict::Verified : Verdict::Refuted;
    return Certificate{
        "arena stayed within its memory budget",
        v, "raw-arena-v1",
        { {"budget", std::to_string(s.budget)},
          {"used", std::to_string(s.used)},
          {"high_water", std::to_string(s.high_water)},
          {"allocations", std::to_string(s.allocations)},
          {"refusals", std::to_string(s.refusals)} }
    };
}
Certificate certificate_with_channels(const ReconcileResult& r, float tolerance,
                                      std::optional<double> motionCoherence,
                                      std::optional<double> hdrHeadroom){
    Certificate c = certificate_from_reconcile(r, tolerance);
    ChannelFidelity ch;
    // AO fidelity only when there was data to reconcile; otherwise null, not faked.
    ch.aoFidelity = (r.pixels > 0)
                  ? std::optional<double>(aoFidelityFromRmse(r.rmse))
                  : std::nullopt;
    ch.motionCoherence = motionCoherence;   // caller passes nullopt when absent
    ch.hdrHeadroom = hdrHeadroom;            // caller passes nullopt when absent
    c.channels = ch;
    return c;
}
}
