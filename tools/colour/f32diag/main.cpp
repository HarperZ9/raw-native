// f32diag PIPELINE MODE SEED ULPS IN.f32 OUT.f32: the colour reference with the
// transcendentals swapped while the transform runs (diagmath.hpp). Tables are built exact.
#include "raw/renderer/colour.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main(int argc, char** argv){
    if (argc != 7){ std::fputs("usage: f32diag PIPELINE MODE SEED ULPS IN OUT\n", stderr); return 2; }
    raw::colour::Pipeline p;
    if (!raw::colour::parsePipeline(argv[1], p)) return 3;
    const raw::colour::Transform t(p);
    std::ifstream f(argv[5], std::ios::binary | std::ios::ate);
    const auto n = (size_t)f.tellg() / sizeof(raw::colour::RGB);
    std::vector<raw::colour::RGB> v(n);
    f.seekg(0); f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(n * sizeof(raw::colour::RGB)));
    diag::g_mode = std::atoi(argv[2]);
    diag::g_state = std::strtoull(argv[3], nullptr, 10) * 2654435761ull + 1;
    diag::g_ulps = (float)std::atof(argv[4]);
    for (auto& c : v) c = t.apply(c);
    diag::g_mode = 0;
    std::ofstream o(argv[6], std::ios::binary);
    o.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(n * sizeof(raw::colour::RGB)));
    return o ? 0 : 4;
}
