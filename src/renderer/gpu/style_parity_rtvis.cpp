// The one-ray visibility estimator's check: the CPU link only (rule 6), bounds and controls of
// evidence/cpp-port-style-bounds-9.json, written before the first run. Each case is reported with
// gpuRun false; StyleParity::gpuPass does not ask a CPU-only pass for a GPU link.
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/style_rtvis.hpp"
#include "style_fixture.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gpu_check {
namespace {
using assets::Json;
namespace fx = style_fx;

// The visibility of every pixel of the yard for one estimator, with the smallest decision margin of
// each pixel's samples.
void yard(const std::string& method, const style::YardLight& L, const style::YardBlocker& b, int N, std::vector<double>& V, std::vector<double>* margin) {
    V.assign(std::size_t(N) * N, 0.0);
    if (margin) margin->assign(V.size(), 1e300);
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        const style::RV3 P = style::yardPoint(x, y);
        const std::vector<style::RV3> Q = style::rtvisSamples(method, L, P, x, y, 0);
        int n = 0;
        for (const style::RV3& q : Q) {
            if (!style::yardOccluded(b, P, q)) n++;
            if (margin) (*margin)[std::size_t(y) * N + std::size_t(x)] = std::min((*margin)[std::size_t(y) * N + std::size_t(x)], style::yardOcclusionMargin(b, P, q));
        }
        V[std::size_t(y) * N + std::size_t(x)] = double(n) / double(Q.size());
    }
}
}  // namespace

// The rtvis cases; returns the number the bundle holds.
int rtvisParity(const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/rtvis.rnfx", b, r.error)) return 0;
    const auto net = b.arr<std::uint32_t>("net", "u32");
    const auto disc = b.arr<double>("concentric", "f64");
    bool netEq = net.size() == 128;
    for (int k = 0; netEq && k < 64; ++k) netEq = int(net[std::size_t(2 * k)]) == style::rtvisBitrev6(k) && int(net[std::size_t(2 * k + 1)]) == style::rtvisPascal6(k);
    double discMax = 0;
    for (int j = 0; j < 64; ++j) for (int i = 0; i < 64; ++i) {
        const auto p = style::rtvisConcentric((i + 0.5) / 64, (j + 0.5) / 64);
        const std::size_t o = (std::size_t(j) * 64 + std::size_t(i)) * 2;
        discMax = std::max({discMax, std::abs(p[0] - disc[o]), std::abs(p[1] - disc[o + 1])});
    }
    const int N = int(b.head.get("size")->number("size"));
    style::YardLight L, bentL;
    bentL.radius *= 1.05;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "rtvis"; s.id = c.get("id")->s;
        const std::string method = c.get("method")->s;
        const style::YardBlocker blk = style::yardBlocker(c.get("blocker")->s);
        const auto jsV = b.arr<double>(s.id + ".V", "f64");
        const auto jsT = b.arr<double>(s.id + ".tone", "f64");
        std::vector<double> V, margin, bent;
        yard(method, L, blk, N, V, &margin);
        yard(method, bentL, blk, N, bent, nullptr);
        s.presetEqual = netEq && discMax <= 1e-15;
        if (!s.presetEqual) s.presetDetail = std::string(netEq ? "" : "net ") + (discMax <= 1e-15 ? "" : "concentric");
        s.decisions = long(V.size());
        double tmax = 0;
        long bentDiff = 0;
        for (std::size_t i = 0; i < V.size(); ++i) {
            if (V[i] != jsV[i]) (margin[i] <= 1e-9 ? s.cpuExcused : s.cpuMismatch)++;
            const style::RV3 P = style::yardPoint(int(i % std::size_t(N)), int(i / std::size_t(N)));
            tmax = std::max(tmax, std::abs(style::yardTone(P, L, V[i]) - jsT[i]));
            bentDiff += bent[i] != jsV[i];
        }
        s.cpuMaxAbs = tmax;
        s.cpuControlFails = bentDiff > 0;
        s.cpuPass = s.presetEqual && s.cpuMismatch == 0 && double(s.cpuExcused) <= 1e-4 * double(s.decisions) && tmax <= 1e-12 && s.cpuControlFails;
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
