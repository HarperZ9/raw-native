// GTAO, SSR and TAA GPU parity on the linked backend (M3; bounds in evidence/m3-post-bounds.json).
// Without an adapter it exits 77 (skipped, never passed). The scenes include the owned
// assets; CI runs it on WARP with RAW_NATIVE_D3D12_WARP=1.
//   post_parity [out.json]
#include "raw/tools/raster_cmd.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    std::string err;
    if (!raw::rhi::device(err)) { std::printf("skipped: %s\n", err.c_str()); return 77; }
    std::vector<std::string> a{"post-parity"};
    if (argc > 1) { a.push_back("--out"); a.push_back(argv[1]); }
    std::vector<char*> v;
    for (auto& s : a) v.push_back(s.data());
    return raw::postParityCommand(int(v.size()), v.data());
}
