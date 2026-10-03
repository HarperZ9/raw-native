// per-channel fidelity witnesses on the certificate.
#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "check.hpp"
#include <string>
#include <optional>
#include <cmath>
using namespace raw;
int main() {
    // (1) aoFidelityFromRmse: 0 error -> 1.0, monotonic decreasing, clamped [0,1].
    CHECK_NEAR(aoFidelityFromRmse(0.0), 1.0, 1e-9);
    CHECK(aoFidelityFromRmse(0.1) < 1.0 && aoFidelityFromRmse(0.1) > 0.0);
    CHECK(aoFidelityFromRmse(1.0) < aoFidelityFromRmse(0.1));   // more error -> less confidence
    CHECK(aoFidelityFromRmse(-5.0) == 0.0);                     // garbage -> 0, never > 1

    // (2) A DIVERGENT reconcile with all three channels present: the JSON adds a
    //     "channels" object AND preserves the original reconcile claim/verdict/oracle.
    ReconcileResult d; d.rmse = 0.1294f; d.maxError = 0.6094f; d.pixels = 37996; d.withinTolerance = false;
    std::string j = to_json(certificate_with_channels(d, 0.12f,
                                /*motionCoherence=*/std::optional<double>(0.97),
                                /*hdrHeadroom=*/std::optional<double>(10.4)));
    // original reconcile shape preserved
    CHECK(j.find("\"oracle\":\"raw-rt-ao-v1\"") != std::string::npos);
    CHECK(j.find("\"verdict\":\"refuted\"") != std::string::npos);
    CHECK(j.find("[\"rmse\",\"0.1294\"]") != std::string::npos);
    // new per-channel fidelity fields present
    CHECK(j.find("\"channels\":{") != std::string::npos);
    CHECK(j.find("\"ao_fidelity\":") != std::string::npos);
    CHECK(j.find("\"motion_coherence\":0.97") != std::string::npos);
    CHECK(j.find("\"hdr_headroom\":10.4") != std::string::npos);
    // ao_fidelity is the in-range mapping of the rmse, not a faked 1.0
    double expectAo = aoFidelityFromRmse(0.1294);
    CHECK(expectAo > 0.0 && expectAo < 1.0);

    // (3) ABSENT channels are witnessed as JSON null, never faked.
    std::string jn = to_json(certificate_with_channels(d, 0.12f,
                                /*motionCoherence=*/std::nullopt,
                                /*hdrHeadroom=*/std::nullopt));
    CHECK(jn.find("\"motion_coherence\":null") != std::string::npos);
    CHECK(jn.find("\"hdr_headroom\":null") != std::string::npos);
    // ao still present because there were pixels to reconcile
    CHECK(jn.find("\"ao_fidelity\":null") == std::string::npos);

    // (4) NO pixels to reconcile -> ao_fidelity is null (no data, not a fake pass),
    //     and the reconcile verdict is unverifiable.
    ReconcileResult z; z.pixels = 0; z.withinTolerance = false;
    std::string jz = to_json(certificate_with_channels(z, 0.12f, std::nullopt, std::nullopt));
    CHECK(jz.find("\"verdict\":\"unverifiable\"") != std::string::npos);
    CHECK(jz.find("\"ao_fidelity\":null") != std::string::npos);

    // (5) BACKWARD COMPATIBILITY: a certificate WITHOUT channels is byte-identical
    //     to the original {claim,verdict,oracle,evidence} shape (no trailing block).
    Certificate plain{"x", Verdict::Verified, "o", {{"k","v"}}};
    CHECK(to_json(plain) ==
        "{\"claim\":\"x\",\"verdict\":\"verified\",\"oracle\":\"o\","
        "\"evidence\":[[\"k\",\"v\"]]}");
    CHECK(to_json(plain).find("channels") == std::string::npos);
    return raw_test_summary();
}
