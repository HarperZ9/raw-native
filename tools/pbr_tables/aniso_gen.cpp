// Generates the anisotropic split-albedo table that the library embeds
// (src/renderer/data/aniso_albedo.f32; method note 4 of 2026-10-12 in
// evidence/m3-materials2-bounds.json). Run once by hand, then commit both outputs:
//   pbr_aniso_gen src/renderer/data/aniso_albedo.f32
// writes the table (kAnAll float32 values of A4, then kAnAll of B4, little-endian, indexed
// [k][r][phi][mu] as raw::pbr::Tables reads them) and <out>.sha256 (the SHA-256 of the
// table's bytes). Every entry is Tables::anisoEntry at Tables::kAnSamples samples, so the
// table does not depend on the thread count. test_pbr_aniso_table checks the hash and
// regenerates a seeded sample of entries.
#include "raw/renderer/pbr.hpp"
#include "raw/core/parallel.hpp"
#include "raw/core/sha256.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    using raw::pbr::Tables;
    static_assert(std::endian::native == std::endian::little, "the table is written little-endian");
    if (argc != 2) { std::fprintf(stderr, "usage: pbr_aniso_gen <out.f32>\n"); return 2; }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<float> v(2 * Tables::kAnAll);
    const int rows = Tables::kAnK * Tables::kAnRough;
    raw::parallelRows(rows, int(std::max(1u, std::thread::hardware_concurrency())), [&](int row) {
        const int ik = row / Tables::kAnRough, ir = row % Tables::kAnRough;
        for (int ip = 0; ip < Tables::kAnPhi; ++ip) for (int im = 0; im < Tables::kAnMu; ++im) {
            const std::size_t at = (std::size_t(row) * Tables::kAnPhi + ip) * Tables::kAnMu + im;
            double a, b;
            Tables::anisoEntry(ik, ir, ip, im, Tables::kAnSamples, a, b);
            v[at] = float(a); v[Tables::kAnAll + at] = float(b);
        }
    });
    const std::string out = argv[1];
    {
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size() * sizeof(float)));
        if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    }
    const std::string hex = raw::sha256Hex(v.data(), v.size() * sizeof(float));
    {
        std::ofstream h(out + ".sha256", std::ios::binary);
        const std::size_t slash = out.find_last_of("/\\");
        h << hex << "  " << (slash == std::string::npos ? out : out.substr(slash + 1)) << "\n";
        if (!h) { std::fprintf(stderr, "cannot write %s.sha256\n", out.c_str()); return 1; }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("{\"entries\": %zu, \"samples\": %d, \"grid\": [%d, %d, %d, %d], \"bytes\": %zu, \"sha256\": \"%s\", \"seconds\": %.1f}\n",
                Tables::kAnAll, Tables::kAnSamples, Tables::kAnMu, Tables::kAnPhi, Tables::kAnRough, Tables::kAnK,
                v.size() * sizeof(float), hex.c_str(), s);
    return 0;
}
