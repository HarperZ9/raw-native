// The path tracer's lobe energy table, interface v4: see EnergyTable in rt_pt_math.hpp.
#include "rt_pt_math.hpp"
#include "raw/renderer/brdf.hpp"
#include "raw/renderer/pbr.hpp"
#include "raw/core/parallel.hpp"
#include <thread>
namespace raw::rt::pt {
namespace {
constexpr int kSamples = 1 << 12;   // VNDF samples a node (pbr::Tables uses the same count)

EnergyTable build() {
    constexpr int N = EnergyTable::kMu, R = EnergyTable::kRough;
    EnergyTable t;
    t.v.assign(EnergyTable::kFloats, 0.0f);
    // Rows are independent and each is written by one thread: identical for any thread count.
    parallelRows(R, int(std::max(1u, std::thread::hardware_concurrency())), [&](int i) {
        const double r = double(i) / (R - 1), a = std::max(1e-4, r * r);
        for (int j = 0; j < N; ++j) {
            const double u = double(j) / (N - 1), mu = std::max(u * u, 1e-4);
            double A = 0.0, B = 0.0;
            pbr::ggxAlbedoSplit(mu, a, kSamples, A, B);
            t.v[(std::size_t(i) * N + std::size_t(j)) * 2] = float(A);
            t.v[(std::size_t(i) * N + std::size_t(j)) * 2 + 1] = float(B);
        }
    });
    // The cosine-weighted means of the float table as tableAB reads it, so 2 int A(mu) mu dmu is
    // the quadrature of exactly the function the lobe uses.
    std::vector<double> x, w;
    brdf::gaussLegendre01(64, x, w);
    for (int i = 0; i < R; ++i) {
        const float r = float(i) / float(R - 1);
        double sa = 0.0, sb = 0.0;
        for (std::size_t k = 0; k < x.size(); ++k) {
            const AB e = tableAB(t.v.data(), float(x[k]), r);
            sa += w[k] * double(e.a) * x[k]; sb += w[k] * double(e.b) * x[k];
        }
        t.v[EnergyTable::kNodes + std::size_t(i) * 2] = float(2.0 * sa);
        t.v[EnergyTable::kNodes + std::size_t(i) * 2 + 1] = float(2.0 * sb);
    }
    return t;
}
}  // namespace

const EnergyTable& energyTable() {
    static const EnergyTable t = build();
    return t;
}

}  // namespace raw::rt::pt
