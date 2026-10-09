// raw-cert/2 end to end: a render writes full-precision AO, the mask and output
// digests; verify re-derives the verdict from those files alone; any changed
// byte or edited number is caught. Also checks --threads leaves output unchanged
// and --no-rt yields an honest "unverifiable".
#include "raw/tools/run.hpp"
#include "raw/core/arena.hpp"
#include "check.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
using namespace raw;
namespace fs = std::filesystem;
static std::string slurp(const fs::path& p){
    std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void spit(const fs::path& p, const std::string& s){
    std::ofstream f(p, std::ios::binary); f << s; }
static fs::path renderInto(const std::string& tag, CliParams p){
    fs::path dir = fs::current_path() / ("verify-" + tag + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    p.out = dir.string();
    FrameResult o = renderFromParams(p, nullptr);
    FileDigests d = writeFrameFiles(o, p);
    spit(dir / "certificate.json", to_json(aoCertificate(o, p, d)));
    return dir;
}
int main(){
    CliParams p; p.width = 48; p.height = 40;
    fs::path a = renderInto("a", p);
    std::string report;
    CHECK(verifyDir(a.string(), report) == 0);
    std::string cert = slurp(a / "certificate.json");
    CHECK(cert.find("\"schema\":\"raw-cert/2\"") != std::string::npos);
    CHECK(cert.find("\"renderer\":\"raw-native 0.6.0\"") != std::string::npos);
    CHECK(cert.find("\"samples\":{\"rt\":64,\"ss\":24}") != std::string::npos);
    CHECK(cert.find("\"mask.pgm\":\"") != std::string::npos);

    // Same params on 4 threads: identical certificate bytes.
    CliParams p4 = p; p4.threads = 4;
    fs::path b = renderInto("b", p4);
    CHECK(slurp(b / "certificate.json") == cert);

    // Flip one byte of the screen-space AO: the digest check fails.
    std::string ss = slurp(a / "ao_ss.pfm");
    ss[ss.size() - 1] ^= 0x01;
    spit(a / "ao_ss.pfm", ss);
    report.clear();
    CHECK(verifyDir(a.string(), report) == 3);

    // Edit the recorded rmse in an otherwise intact render: the recompute fails.
    std::string edited = slurp(b / "certificate.json");
    size_t k = edited.find("\"exact\":{");
    size_t r = edited.find("\"rmse\":", k);
    CHECK(k != std::string::npos && r != std::string::npos);
    edited.replace(r + 7, 1, edited[r + 7] == '9' ? "8" : "9");
    spit(b / "certificate.json", edited);
    report.clear();
    CHECK(verifyDir(b.string(), report) == 3);

    // A missing file is "missing", not "mismatch".
    fs::remove(a / "mask.pgm");
    report.clear();
    CHECK(verifyDir(a.string(), report) == 2);

    // No reference: verdict unverifiable, and verify accepts exactly that.
    CliParams pn = p; pn.rtao = false;
    fs::path n = renderInto("n", pn);
    CHECK(slurp(n / "certificate.json").find("\"verdict\":\"unverifiable\"") != std::string::npos);
    CHECK(!fs::exists(n / "ao_rt.pfm"));
    report.clear();
    CHECK(verifyDir(n.string(), report) == 0);
    for (const auto& d : {a, b, n}) fs::remove_all(d);
    return raw_test_summary();
}
