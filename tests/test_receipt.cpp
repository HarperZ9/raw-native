// superstack.receipt/1 beside raw-cert/2: the scene matches the contract's
// reference scene, the frame matches the contract's pixel reference, the seal
// holds and breaks on tampering, and every path reports both verdicts.
#include "raw/receipt.hpp"
#include "raw/gpu_reconcile.hpp"
#include "raw/image.hpp"
#include "raw/run.hpp"
#include "raw/sha256.hpp"
#include "superstack.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
namespace ss = superstack;
static CliParams params(int w, int h, bool rt){
    CliParams p; p.width = w; p.height = h; p.rtao = rt; return p;
}
static std::string tol(const ss::Value& r, const char* key){ return r.at("reconcile").at("tolerance").at(key).str(); }
int main(){
    // The default scene is the contract's reference scene, byte for byte
    // (superstack examples/pixels/scene.json, canonical SHA-256; the same in
    // v0.1.0 and v0.2.0).
    const CliParams def = params(256, 256, true);
    CHECK(sha256Hex(sceneJson(def)) == "84aa884f71fc47bf3fcc0a35eb795ac59cbf679f5f398263a27fc7dcca835b36");
    CHECK(sceneJson(params(256, 256, false)) != sceneJson(def));   // a different estimator is a different scene

    const FrameResult o = renderFromParams(def, nullptr);
    // The frame's canonical RGB8 bytes equal the contract's pixel reference,
    // and the ray-traced AO bytes equal the contract's AO reference.
    CHECK(sha256Hex(rgb8Bytes(o.frame)) == "0282ef9cc07a0012da9da3217c164f44d00a3e9f7704ff719770e832c9ca6d44");
    CHECK(sha256Hex(f32Bytes(o.aoRT)) == "d6c34cbb66b64f7dbfe271a6dbc0f094a96657c18f239cdd63dc49a7ef1775a8");

    const std::string text = aoReceiptJson(o, def, "raw-native-cpu", {{"frame.ppm", std::string(64, 'a')}});
    CHECK(verifyReceiptText(text).empty());
    const ss::Value r = ss::parse(text);
    CHECK(ss::canonical(r) == text);                                    // written in canonical form
    CHECK(r.at("reconcile").at("identity").str() == "DRIFT");          // two estimators never share bytes
    CHECK(tol(r, "verdict") == (o.rec.withinTolerance ? "verified" : "refuted"));
    CHECK(r.at("seed_rule").str() == "raw-pixel-hash/1");
    CHECK(!r.at("does_not_prove").arr().empty());
    // One changed character breaks the seal.
    std::string tampered = text;
    tampered.replace(tampered.find("\"refuted\""), 9, "\"verified\"");
    CHECK(!verifyReceiptText(tampered).empty());
    CHECK(verifyReceiptText("{not json").size() == 1);

    // Without the ray-traced reference the tolerance verdict is unverifiable, with a reason.
    const CliParams nort = params(64, 64, false);
    const ss::Value u = ss::parse(aoReceiptJson(renderFromParams(nort, nullptr), nort, "raw-native-cpu", {}));
    CHECK(verifyReceiptText(ss::canonical(u)).empty());
    CHECK(tol(u, "verdict") == "unverifiable" && !tol(u, "reason").empty());
    CHECK(u.at("reconcile").at("reference").at("content_sha256").str() == "none");

    // GPU receipt: a frame against itself is MATCH and verified; no GPU frame is
    // DRIFT and unverifiable with the reason.
    const CliParams small = params(64, 64, true);
    const FrameResult f = renderFromParams(small, nullptr);
    const GpuReconcile same = reconcileGpuCpu(f, f, true);
    CHECK(!same.gpuFrameSha256.empty() && same.gpuFrameSha256 == same.cpuFrameSha256);
    const ss::Value g = ss::parse(gpuReceiptJson(&f, &f, same, small, {}));
    CHECK(verifyReceiptText(ss::canonical(g)).empty());
    CHECK(g.at("reconcile").at("identity").str() == "MATCH");
    CHECK(tol(g, "verdict") == "verified");
    CHECK(g.at("reconcile").at("tolerance").at("metrics").find("ao_rt_rmse") != nullptr);
    CHECK(gpuCertificateJson(same, {}, "t", "{}", 0, 0).find("\"identity\":\"MATCH\"") != std::string::npos);
    GpuReconcile none; none.reason = "no hardware D3D12 adapter";
    const ss::Value n = ss::parse(gpuReceiptJson(nullptr, nullptr, none, small, {}));
    CHECK(verifyReceiptText(ss::canonical(n)).empty());
    CHECK(n.at("reconcile").at("identity").str() == "DRIFT");
    CHECK(tol(n, "verdict") == "unverifiable" && tol(n, "reason") == "no hardware D3D12 adapter");
    CHECK(gpuCertificateJson(none, {}, "t", "{}", 0, 0).find("\"identity\":null") != std::string::npos);
    // A GPU frame that differs by one level in one pixel: DRIFT, still verified.
    FrameResult off = f;
    for (auto& px : off.frame.px) if (px.x > 0.1f && px.x < 0.9f){ px.x += 1.0f / 255.0f; break; }
    const GpuReconcile drift = reconcileGpuCpu(off, f, true);
    const ss::Value d = ss::parse(gpuReceiptJson(&off, &f, drift, small, {}));
    CHECK(d.at("reconcile").at("identity").str() == "DRIFT");
    CHECK(tol(d, "verdict") == "verified");
    return raw_test_summary();
}
