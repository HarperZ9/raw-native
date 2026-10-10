// The sampled-texture identity check on the linked RHI backend (ROADMAP M2 criterion 3;
// bounds in evidence/m2-rhi-texture-bounds.json). Without an adapter it exits 77, which
// CTest reports as skipped with the reason, never as passed. CI runs it on WARP with
// RAW_NATIVE_D3D12_WARP=1, where 77 fails the step.
//   texture_identity [out.json]
#include "raw/renderer/texture_identity.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
int main(int argc, char** argv) {
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev) { std::printf("skipped: %s\n", err.c_str()); return 77; }
    // --dxil VS FS: run the check with DXIL from files (the A5 spike's Slang output).
    std::vector<char> vsb, fsb;
    auto load = [](const char* path, std::vector<char>& b) { std::ifstream f(path, std::ios::binary); b.assign(std::istreambuf_iterator<char>(f), {}); return !b.empty(); };
    const bool files = argc > 4 && std::string(argv[2]) == "--dxil" && load(argv[3], vsb) && load(argv[4], fsb);
    const raw::gpu_check::TextureIdentity r = files
        ? raw::gpu_check::textureIdentity(*dev, {raw::rhi::ShaderFormat::Dxil, vsb.data(), vsb.size()}, {raw::rhi::ShaderFormat::Dxil, fsb.data(), fsb.size()})
        : raw::gpu_check::textureIdentity(*dev);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
