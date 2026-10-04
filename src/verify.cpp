#include "raw/run.hpp"
#include "raw/image.hpp"
#include "raw/reconcile.hpp"
#include "raw/sha256.hpp"
#include "raw/receipt.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdlib>
namespace raw {
namespace {
// Small readers for the certificate this program writes. They are not a
// general JSON parser: they expect the exact layout of to_json().
bool readText(const std::string& path, std::string& out){
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
// Raw text of the value after "key": inside `j`, ending at ',' or '}'.
bool scalarAfter(const std::string& j, const std::string& key, std::string& out){
    size_t k = j.find("\"" + key + "\":");
    if (k == std::string::npos) return false;
    size_t s = k + key.size() + 3, e = s;
    while (e < j.size() && j[e] != ',' && j[e] != '}') ++e;
    out = j.substr(s, e - s);
    if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
    return !out.empty();
}
// The object body "{...}" that follows "key": (no nested objects inside).
bool objectAfter(const std::string& j, const std::string& key, std::string& out){
    size_t k = j.find("\"" + key + "\":{");
    if (k == std::string::npos) return false;
    size_t s = k + key.size() + 3, e = j.find('}', s);
    if (e == std::string::npos) return false;
    out = j.substr(s, e - s + 1);
    return true;
}
FileDigests parseOutputs(const std::string& obj){
    FileDigests d;
    size_t i = 0;
    while (true){
        size_t a = obj.find('"', i);       if (a == std::string::npos) break;
        size_t b = obj.find('"', a + 1);   if (b == std::string::npos) break;
        size_t c = obj.find('"', b + 1);   if (c == std::string::npos) break;
        size_t e = obj.find('"', c + 1);   if (e == std::string::npos) break;
        d.emplace_back(obj.substr(a + 1, b - a - 1), obj.substr(c + 1, e - c - 1));
        i = e + 1;
    }
    return d;
}
struct Tally {
    std::string& report; int mismatches{0};
    void check(bool ok, const std::string& what, const std::string& detail){
        report += (ok ? "MATCH    " : "MISMATCH ") + what + (detail.empty() ? "" : "  " + detail) + "\n";
        if (!ok) ++mismatches;
    }
};
bool hasFile(const FileDigests& d, const std::string& name){
    for (const auto& kv : d) if (kv.first == name) return true;
    return false;
}
// Recompute the reconcile from the full-precision files and compare.
int recheckReconcile(const std::string& dir, const std::string& exact,
                     const std::string& verdict, Tally& t){
    Buffer<float> rt, ss; Buffer<uint8_t> mask;
    if (!readPFM1(dir + "/ao_rt.pfm", rt) || !readPFM1(dir + "/ao_ss.pfm", ss)
        || !readMaskPGM(dir + "/mask.pgm", mask)){
        t.report += "MISSING  ao_rt.pfm, ao_ss.pfm or mask.pgm unreadable\n"; return 2; }
    if (rt.w != ss.w || rt.h != ss.h || rt.w != mask.w || rt.h != mask.h){
        t.report += "MISMATCH buffer sizes differ\n"; return 3; }
    std::string pixels, rmse, maxError, tolerance;
    if (!scalarAfter(exact, "pixels", pixels) || !scalarAfter(exact, "rmse", rmse)
        || !scalarAfter(exact, "maxError", maxError) || !scalarAfter(exact, "tolerance", tolerance)){
        t.report += "MISSING  exact block incomplete\n"; return 2; }
    float tol = std::strtof(tolerance.c_str(), nullptr);
    ReconcileResult r = reconcile(ss, rt, mask, tol);
    const char* v = r.pixels == 0 ? "unverifiable" : (r.withinTolerance ? "verified" : "refuted");
    t.check(std::to_string(r.pixels) == pixels, "pixels", std::to_string(r.pixels) + " vs " + pixels);
    t.check(exactFloat(r.rmse) == rmse, "rmse", exactFloat(r.rmse) + " vs " + rmse);
    t.check(exactFloat(r.maxError) == maxError, "maxError", exactFloat(r.maxError) + " vs " + maxError);
    t.check(verdict == v, "verdict", std::string(v) + " vs " + verdict);
    return 0;
}
}
int verifyDir(const std::string& dir, std::string& report){
    std::string cert, outputsObj, exact, verdict;
    if (!readText(dir + "/certificate.json", cert)){ report += "MISSING  certificate.json\n"; return 2; }
    if (cert.find("\"schema\":\"raw-cert/2\"") == std::string::npos
        || !objectAfter(cert, "outputs", outputsObj) || !objectAfter(cert, "exact", exact)
        || !scalarAfter(cert, "verdict", verdict)){
        report += "MISSING  certificate.json is not raw-cert/2\n"; return 2; }
    Tally t{report};
    FileDigests outputs = parseOutputs(outputsObj);
    for (const auto& [name, digest] : outputs){
        std::string actual = sha256File(dir + "/" + name);
        if (actual.empty()){ report += "MISSING  " + name + "\n"; return 2; }
        t.check(actual == digest, "sha256 " + name, actual == digest ? "" : actual + " vs " + digest);
    }
    if (hasFile(outputs, "ao_rt.pfm")){
        if (int rc = recheckReconcile(dir, exact, verdict, t); rc) return rc;
    } else {
        t.check(verdict == "unverifiable", "verdict without reference", verdict);
    }
    // From 0.5.0 a superstack receipt sits beside the certificate; older
    // directories have none, which is reported and is not a mismatch.
    if (std::filesystem::exists(dir + "/receipt.json")) t.mismatches += checkReceiptDir(dir, verdict, report);
    else report += "ABSENT   receipt.json (written from 0.5.0)\n";
    report += t.mismatches == 0 ? "verify: all checks match\n"
                                : "verify: " + std::to_string(t.mismatches) + " mismatch(es)\n";
    return t.mismatches == 0 ? 0 : 3;
}
}
