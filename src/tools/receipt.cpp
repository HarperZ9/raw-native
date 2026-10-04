#include "raw/tools/receipt.hpp"
#include "raw/cert/gpu_tolerance.hpp"
#include "raw/core/image.hpp"
#include "raw/core/sha256.hpp"
#include "raw/core/version.hpp"
#if RAW_NATIVE_SUPERSTACK
#include "superstack.hpp"   // third_party/superstack, MIT, pinned by SUPERSTACK.sha256
#endif
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <span>
#if RAW_NATIVE_SUPERSTACK
namespace raw {
namespace ss = superstack;
namespace {
// A float as the double its shortest decimal names, so 0.9f is written 0.9
// and not 0.8999999761581421. Every float raw-native records goes through this.
double f2d(float v){
    char b[48];
    auto r = std::to_chars(b, b + sizeof b, v);
    double d = 0;
    std::from_chars(b, r.ptr, d);
    return d;
}
ss::Value vec3(const Vec3& v){ return ss::Value(ss::Array{f2d(v.x), f2d(v.y), f2d(v.z)}); }
ss::Value vec3(double x, double y, double z){ return ss::Value(ss::Array{x, y, z}); }
std::string producerVersion(){
    const std::string v = version();
    const size_t sp = v.rfind(' ');
    return sp == std::string::npos ? v : v.substr(sp + 1);
}
ss::Value camera(const Vec3& eye, const Vec3& target, const Vec3& up, float fovy){
    ss::Value c;
    c.set("eye", vec3(eye)); c.set("target", vec3(target)); c.set("up", vec3(up)); c.set("fovy", f2d(fovy));
    return c;
}
// The scene of src/scene.cpp, in the field layout of the contract's reference scene.
ss::Value scene(const CliParams& p){
    ss::Value frame;
    frame.set("width", p.width); frame.set("height", p.height);
    frame.set("pixel_center", 0.5); frame.set("origin", "top-left");
    ss::Value ground, box, light, shade, ao;
    ground.set("id", "ground"); ground.set("shape", "quad");
    ground.set("corners", ss::Value(ss::Array{vec3(-5, 0, -5), vec3(5, 0, -5), vec3(5, 0, 5), vec3(-5, 0, 5)}));
    ground.set("normal", vec3(0, 1, 0)); ground.set("albedo", vec3(Vec3{0.7f, 0.7f, 0.7f}));
    box.set("id", "box"); box.set("shape", "box"); box.set("center", vec3(0, 1, 0)); box.set("half", 1);
    box.set("albedo", vec3(Vec3{0.8f, 0.3f, 0.2f}));
    light.set("kind", "directional"); light.set("dir", vec3(Vec3{-0.4f, -1.0f, -0.3f})); light.set("intensity", 1);
    shade.set("model", "raw-lambert-ao/1"); shade.set("ambient", 0.2);
    shade.set("clamp", ss::Value(ss::Array{0, 1})); shade.set("encode", "u8-round-half-up");
    ao.set("estimator", p.rtao ? "rt-cosine-hemisphere" : "screen-space");
    ao.set("samples", p.rtao ? kRtSamples : kSsSamples); ao.set("radius", f2d(kAoRadius));
    ao.set("hash", "raw-pixel-hash/1"); ao.set("origin_offset", 0.001); ao.set("t_min", 0.0001);
    ss::Value s;
    s.set("kind", "superstack.scene/1");
    s.set("seed", "raw-default");
    s.set("t_flicks", 0);
    s.set("frame", frame);
    s.set("camera", camera(p.eye, p.center, p.up, p.fovy));
    if (p.hasPrevCamera()){
        const Camera pc = prevCameraFromParams(p);
        s.set("prev_camera", camera(pc.eye, pc.center, pc.up, pc.fovy));
    }
    s.set("meshes", ss::Value(ss::Array{ground, box}));
    s.set("lights", ss::Value(ss::Array{light}));
    s.set("shade", shade);
    s.set("ao", ao);
    ss::Array outs{"frame.rgb8"};
    if (p.rtao) outs.emplace_back("ao.f32");
    outs.emplace_back("mask.u8");
    s.set("outputs", ss::Value(std::move(outs)));
    return s;
}
ss::Value outputsValue(const FileDigests& d){
    ss::Value o{ss::Object{}};
    for (const auto& [name, digest] : d) o.set(name, digest);
    return o;
}
ss::Value media(const CliParams& p, const char* format, const char* transfer){
    ss::Value m;
    m.set("kind", "image"); m.set("width", p.width); m.set("height", p.height);
    m.set("format", format); m.set("transfer", transfer);
    return m;
}
ss::Value reference(const std::string& backend, const std::string& sha){
    ss::Value r;
    r.set("backend", backend);
    r.set("content_sha256", sha.empty() ? std::string("none") : sha);   // "none": no reference bytes exist
    return r;
}
// The two verdicts. identity compares the receipt's subject with the reference.
ss::Value reconcileBlock(const std::string& refSha, const std::string& subjectSha, const std::string& verdict,
                         ss::Value metrics, ss::Value bounds, const std::string& reason){
    ss::Value tol;
    tol.set("verdict", verdict);
    tol.set("metrics", std::move(metrics));
    tol.set("bounds", std::move(bounds));
    if (!reason.empty()) tol.set("reason", reason);
    ss::Value b;
    b.set("identity", ss::identity(refSha.empty() ? "none" : refSha, subjectSha));
    b.set("tolerance", std::move(tol));
    return b;
}
std::string seal(ss::ReceiptArgs& a, const std::string& content){
    a.producer = "raw-native"; a.version = producerVersion();
    a.seed_rule = "raw-pixel-hash/1";
    a.content = std::span<const std::uint8_t>((const std::uint8_t*)content.data(), content.size());
    return ss::canonical(ss::make_receipt(a));
}
}  // namespace

bool receiptsCompiled(){ return true; }
std::string sceneJson(const CliParams& p){ return ss::canonical(scene(p)); }

std::string aoReceiptJson(const FrameResult& o, const CliParams& p, const std::string& backend,
                          const FileDigests& outputs){
    const std::string subject = f32Bytes(o.aoSS);
    const std::string refBytes = p.rtao ? f32Bytes(o.aoRT) : std::string();
    const std::string subjectSha = sha256Hex(subject), refSha = p.rtao ? sha256Hex(refBytes) : std::string();
    ss::Value metrics{ss::Object{}}, bounds;
    bounds.set("ao_rmse_max", f2d(p.tolerance));
    std::string verdict = "unverifiable", reason;
    if (!p.rtao) reason = "the ray-traced reference was skipped (--no-rt)";
    else if (o.rec.pixels == 0) reason = "no covered pixels to compare";
    else {
        metrics.set("ao_rmse", f2d(o.rec.rmse));
        metrics.set("ao_max_error", f2d(o.rec.maxError));
        metrics.set("pixels", o.rec.pixels);
        verdict = o.rec.withinTolerance ? "verified" : "refuted";
    }
    FileDigests all = outputs;
    all.emplace_back("frame.rgb8", sha256Hex(rgb8Bytes(o.frame)));   // canonical frame bytes, for cross-engine checks
    ss::ReceiptArgs a;
    a.backend = backend + "-ssao";
    a.scene = scene(p);
    a.media = media(p, "f32", "linear");
    a.outputs = outputsValue(all);
    a.reference = reference(backend + "-rt-ao-64spp", refSha);
    a.reconcile = reconcileBlock(refSha, subjectSha, verdict, std::move(metrics), std::move(bounds), reason);
    a.does_not_prove = {
        "The subject is the screen-space AO and the reference is the ray-traced AO, two different estimators, so identity reads DRIFT by design; the tolerance verdict is the claim.",
        "The ray-traced reference is itself a 64-sample estimate with visible grain, not ground truth.",
        "An RMSE bound limits the average difference, not the worst pixel.",
        "One built-in scene. No claim about other geometry, lights or materials."};
    return seal(a, subject);
}

std::string gpuReceiptJson(const FrameResult* gpu, const FrameResult* cpu, const GpuReconcile& r,
                           const CliParams& p, const FileDigests& outputs){
    namespace tol = gpu_tolerance;
    const std::string subject = gpu ? rgb8Bytes(gpu->frame) : std::string();
    const std::string subjectSha = sha256Hex(subject);
    const std::string refSha = cpu ? sha256Hex(rgb8Bytes(cpu->frame)) : std::string();
    ss::Value metrics{ss::Object{}}, bounds;
    bounds.set("coverage_mismatch_frac_max", tol::kMaskMismatchFraction);
    const std::pair<const char*, double> b[] = {{"depth_rel", tol::kDepthRelRmse}, {"position", tol::kPositionRmse},
        {"normal", tol::kNormalRmse}, {"motion", tol::kMotionRmse}, {"ao_ss", tol::kAoSsRmse}, {"ao_rt", tol::kAoRtRmse},
        {"frame", tol::kFrameRmse}};
    for (const auto& [name, bound] : b) bounds.set(std::string(name) + "_rmse_max", bound);
    bounds.set("ao_rmse_delta_max", tol::kReconcileRmseDelta);
    std::string verdict = "unverifiable", reason = r.reason;
    if (r.sizeMatch && gpu && cpu){
        metrics.set("coverage_mismatch_frac", r.maskMismatchFraction);
        for (const auto& c : r.channels){
            metrics.set(c.name + "_rmse", c.rmse);
            metrics.set(c.name + "_max_error", c.maxError);
        }
        if (r.rt) metrics.set("ao_rmse_delta", std::fabs((double)r.gpuAoRmse - (double)r.cpuAoRmse));
        verdict = r.pass() ? "verified" : "refuted";
    } else if (reason.empty()) reason = "the GPU frame and the CPU reference differ in size";
    const std::string backend = "raw-native-" + (p.gpuBackend.empty() ? std::string("none") : p.gpuBackend);
    ss::ReceiptArgs a;
    a.backend = backend;
    a.scene = scene(p);
    a.media = media(p, "rgb8", "linear-u8");
    a.outputs = outputsValue(outputs);
    a.reference = reference("raw-native-cpu", refSha);
    a.reconcile = reconcileBlock(refSha, subjectSha, verdict, std::move(metrics), std::move(bounds), reason);
    a.does_not_prove = {
        "Identity compares the 8-bit frames only; the tolerance verdict covers every channel at float precision.",
        "Agreement with the CPU path is not agreement with ground truth: the CPU ray-traced AO is itself a 64-sample estimate.",
        "Checked on the adapter and driver named in gpu_certificate.json only. Another GPU, driver or browser may differ.",
        "An RMSE bound limits the average difference, not the worst pixel. Maximum errors are reported, not bounded.",
        "One built-in scene. No claim about other geometry, lights or materials."};
    return seal(a, subject);
}

bool writeAoReceipt(const FrameResult& o, const CliParams& p, const std::string& backend,
                    const FileDigests& imageOutputs){
    FileDigests all = imageOutputs;
    for (const char* f : {"certificate.json", "channels.json"}){
        std::string d = sha256File(p.out + "/" + f);
        if (d.empty()) return false;
        all.emplace_back(f, d);
    }
    std::ofstream f(p.out + "/receipt.json", std::ios::binary);
    f << aoReceiptJson(o, p, backend, all);
    return (bool)f;
}

std::vector<std::string> verifyReceiptText(const std::string& json){
    try { return ss::verify_receipt(ss::parse(json)); }
    catch (const std::exception&){ return {"parse"}; }
}
}
namespace raw {
namespace {
std::string fileBytes(const std::string& path, bool& ok){
    std::ifstream f(path, std::ios::binary);
    ok = (bool)f;
    return ok ? std::string(std::istreambuf_iterator<char>(f), {}) : std::string();
}
// The RGB8 body of a binary PPM as writePPM writes it: three header lines, then pixels.
std::string ppmBody(const std::string& ppm){
    size_t i = 0;
    for (int line = 0; line < 3 && i != std::string::npos; ++line) i = ppm.find('\n', i) + 1;
    return i == 0 || i > ppm.size() ? std::string() : ppm.substr(i);
}
}  // namespace
int checkReceiptDir(const std::string& dir, const std::string& certVerdict, std::string& report){
    bool ok = false;
    const std::string text = fileBytes(dir + "/receipt.json", ok);
    if (!ok){ report += "MISSING  receipt.json\n"; return 1; }
    int bad = 0;
    auto check = [&](bool good, const std::string& what, const std::string& detail){
        report += (good ? "MATCH    " : "MISMATCH ") + what + (detail.empty() ? "" : "  " + detail) + "\n";
        if (!good) ++bad;
    };
    const std::vector<std::string> errs = verifyReceiptText(text);
    std::string joined;
    for (const auto& e : errs) joined += (joined.empty() ? "" : ",") + e;
    check(errs.empty(), "receipt well formed and sealed", joined);
    if (!errs.empty()) return bad;
    const ss::Value rec = ss::parse(text);
    // Every listed file, and the canonical frame bytes, hash as recorded.
    for (const auto& m : rec.at("outputs").obj()){
        std::string actual;
        if (m.key == "frame.rgb8"){
            bool have = false;
            const std::string body = ppmBody(fileBytes(dir + "/frame.ppm", have));
            actual = have && !body.empty() ? sha256Hex(body) : std::string();
        } else actual = sha256File(dir + "/" + m.key);
        check(actual == m.value.str(), "receipt sha256 " + m.key, actual == m.value.str() ? "" : actual);
    }
    // The subject and the reference, recomputed from the full-precision AO files.
    Buffer<float> ss_, rt;
    if (!readPFM1(dir + "/ao_ss.pfm", ss_)){ report += "MISSING  ao_ss.pfm\n"; return bad + 1; }
    const std::string subject = sha256Hex(f32Bytes(ss_));
    check(subject == rec.at("content_sha256").str(), "receipt content_sha256 (ao_ss.f32)", "");
    const ss::Value& rc = rec.at("reconcile");
    const std::string refSha = readPFM1(dir + "/ao_rt.pfm", rt) ? sha256Hex(f32Bytes(rt)) : std::string("none");
    check(rc.at("reference").at("content_sha256").str() == refSha, "receipt reference (ao_rt.f32)", "");
    const std::string tv = rc.at("tolerance").at("verdict").str();
    check(tv == certVerdict, "receipt tolerance verdict", tv + " vs " + certVerdict);
    return bad;
}
}
#else
// A standard library that cannot compile superstack.hpp: no receipts. The CLI
// reports why (receiptsCompiled), and verify reports the file as absent.
namespace raw {
bool receiptsCompiled(){ return false; }
std::string sceneJson(const CliParams&){ return {}; }
std::string aoReceiptJson(const FrameResult&, const CliParams&, const std::string&, const FileDigests&){ return {}; }
std::string gpuReceiptJson(const FrameResult*, const FrameResult*, const GpuReconcile&, const CliParams&, const FileDigests&){ return {}; }
bool writeAoReceipt(const FrameResult&, const CliParams&, const std::string&, const FileDigests&){ return true; }
std::vector<std::string> verifyReceiptText(const std::string&){ return {"unsupported"}; }
int checkReceiptDir(const std::string&, const std::string&, std::string& report){
    report += "MISMATCH receipt.json present, but this build cannot read superstack receipts\n"; return 1; }
}
#endif
