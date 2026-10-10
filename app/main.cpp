#include "raw/assets/json.hpp"
#include "raw/renderer/texture_identity.hpp"
#include "raw/rhi/rhi.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/accel.hpp"
#include "raw/renderer/ray_ao.hpp"
#include "raw/renderer/ssao.hpp"
#include "raw/cert/reconcile.hpp"
#include "raw/renderer/composite.hpp"
#include "raw/cert/certificate.hpp"
#include "raw/renderer/render.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/tools/channels_json.hpp"
#include "raw/math/mat.hpp"
#include "raw/core/arena.hpp"
#include "raw/core/version.hpp"
#include "raw/tools/run.hpp"
#include "raw/tools/gpu_run.hpp"
#include "raw/renderer/gpu.hpp"
#include "raw/tools/receipt.hpp"
#include "raw/tools/threads_cmd.hpp"
#include "raw/tools/colour_cmd.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <optional>
#include <utility>
#include <cstring>
#include <filesystem>
#include <system_error>
using namespace raw;
// Render, write every file and certificate, and print the summary for a frame
// rendered inside `arena`.
static int emitOutputs(const FrameResult& o, const CliParams& p, Arena& arena){
    const std::string out = p.out;
    FileDigests outputs = writeFrameFiles(o, p);
    for (const auto& kv : outputs){
        if (kv.second.empty()){ std::printf("could not write %s/%s\n", out.c_str(), kv.first.c_str()); return 2; }
    }
    Certificate aoCert    = aoCertificate(o, p, outputs);
    Certificate arenaCert = certificate_from_arena(arena.stats());
    std::ofstream(out + "/certificate.json")        << to_json(aoCert);
    std::ofstream(out + "/arena_certificate.json")  << to_json(arenaCert);
    // The certificate plus compact channel summaries, so a caller need not
    // parse the image files. Absent channels are honest null.
    std::ofstream(out + "/channels.json") << channelsJson(o, p);
    // superstack.receipt/1: the same AO check in the shared contract's form.
    if (!writeAoReceipt(o, p, "raw-native-cpu", outputs)){
        std::printf("could not write %s/receipt.json\n", out.c_str()); return 2; }
    if (!receiptsCompiled())
        std::printf("receipt: not written; superstack.hpp does not compile with this build's standard library\n");
    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        o.rec.pixels, o.rec.rmse, o.rec.maxError,
        o.rec.pixels == 0 ? "NO-REFERENCE" : (o.rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT"));
    std::printf("certificate: %s\n", to_json(aoCert).c_str());
    std::printf("arena: %s\n", arena_witness(arena.stats()).c_str());
    std::printf("arena-certificate: %s\n", to_json(arenaCert).c_str());
    return 0;
}
static const char* kUsage =
    "usage: raw_native_cli [outdir] [flags]\n"
    "       raw_native_cli verify <dir>   recheck a render's files against its certificate\n"
    "       raw_native_cli threads [flags]  render the Threads module on the GPU backend\n"
    "         (--world 0..14 --frames --width --height --particles --substeps --fps --persistence\n"
    "          --exposure --spacing --levels --out); writes threads.ppm\n"
    "       raw_native_cli colour list | grid OUT | apply PIPELINE IN OUT | tables PIPELINE OUT.json\n"
    "         the colour reference: tone mappers and display encodings on float32 RGB\n"
    "       raw_native_cli texture-identity\n"
    "         sample a texture through four samplers on the GPU backend and compare with the CPU\n"
    "         sampler reference (JSON on stdout; exit 0 pass, 1 fail, 4 no GPU backend or adapter)\n"
    "  --out <dir>                 output directory (default .)\n"
    "  --width <int> --height <int> frame size (default 256x256)\n"
    "  --eye x,y,z --target x,y,z --up x,y,z   camera\n"
    "  --fovy <radians>            vertical field of view (default 0.9)\n"
    "  --prev-eye x,y,z --prev-target x,y,z --prev-up x,y,z   previous camera for motion\n"
    "  --tolerance <rmse>          AO verdict bound, recorded in the certificate (default 0.12)\n"
    "  --no-rt                     skip the ray-traced reference; verdict is unverifiable\n"
    "  --threads <n>               render threads (default 1); output is identical for any n\n"
    "  --bench <runs>              time <runs> renders, print JSON, write no files\n"
    "  --model <file.gltf|.glb>    render a glTF model on the ground plane in place of the box\n"
    "  --gpu                       render on the GPU backend (D3D12 or WebGPU builds only) and\n"
    "                              write gpu_certificate.json against the CPU reference in <out>/cpu\n"
    "  --params <file.json>        load parameters first; later flags override\n"
    "  --version                   print the version and exit\n"
    "  --help                      print this text and exit\n"
    "exit codes: 0 rendered or verified, 1 memory budget breached (fail-closed),\n"
    "            2 bad input or missing files, 3 verify found a mismatch,\n"
    "            4 --gpu asked for and no GPU backend or adapter is available\n";
// Answer --help and --version before parsing render flags. Returns -1 to continue.
static int infoFlags(int argc, char** argv){
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0){
            std::printf("%s\n%s", version(), kUsage); return 0;
        }
        if (std::strcmp(argv[i], "--version") == 0){ std::printf("%s\n", version()); return 0; }
    }
    return -1;
}
int main(int argc, char** argv){
    if (int rc = infoFlags(argc, argv); rc >= 0) return rc;
    if (argc >= 2 && std::strcmp(argv[1], "threads") == 0) return threadsCommand(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "colour") == 0) return colourCommand(argc - 1, argv + 1);
    // The sampled-texture identity check on this build's GPU backend (ROADMAP M2
    // criterion 3): JSON on stdout; exit 0 when it passes, 1 when it fails, 4 without
    // a GPU backend or adapter.
    if (argc >= 2 && std::strcmp(argv[1], "texture-identity") == 0){
        std::string why;
        raw::rhi::Device* dev = raw::rhi::device(why);
        if (!dev){ std::printf("{\n \"error\": \"%s\"\n}\n", why.c_str()); return 4; }
        const raw::gpu_check::TextureIdentity r = raw::gpu_check::textureIdentity(*dev);
        std::fputs(r.json().c_str(), stdout);
        return r.pass() ? 0 : 1;
    }
    if (argc >= 2 && std::strcmp(argv[1], "verify") == 0){
        if (argc != 3){ std::printf("usage: raw_native_cli verify <dir>\n"); return 2; }
        std::string report;
        int rc = verifyDir(argv[2], report);
        std::printf("%s", report.c_str());
        return rc;
    }
    std::string err;
    std::optional<CliParams> parsed = parseArgs(argc, argv, err);
    if (!parsed){ std::printf("param error: %s\n", err.c_str()); return 2; }
    CliParams p = *parsed;
    if (p.gpu) p.gpuBackend = gpuBackendName();
    if (p.gpu && p.bench > 0){ std::printf("%s\n", benchGpuJson(p).c_str()); return 0; }
    if (p.gpu) return runGpu(p);
    if (p.bench > 0){ std::printf("%s\n", benchJson(p).c_str()); return 0; }
    const std::string out = p.out;
    const int W = p.width, H = p.height;
    // Create the output directory up front. A render whose files cannot be
    // written is bad setup (exit 2), never a silent success.
    std::error_code dirErr;
    std::filesystem::create_directories(out, dirErr);
    if (dirErr || !std::filesystem::is_directory(out)){
        std::printf("cannot create output directory: %s\n", out.c_str()); return 2; }

    // PASS 1 - MEASURE the footprint in a computed generous slab (no magic constant).
    const std::size_t PER_PIXEL_UPPER =
        sizeof(float) + 3*sizeof(Vec3) + sizeof(std::uint8_t)   // gbuffer: depth+nrm/pos/alb+mask
        + sizeof(Vec2)                                          // gbuffer: motion plane
        + 3*sizeof(float) + 2*sizeof(Vec3);                     // aoRT + aoSS + errorMap + frame + hdr
    // The per-pixel bound covers the built-in scene. A --model scene also holds the model's
    // geometry and its acceleration structure, so the measure slab doubles until the
    // render fits (up to 4 GiB, 2 GiB on WebAssembly); pass 2 still runs in exactly the measured footprint.
    std::size_t slabUB = (std::size_t)W*H*PER_PIXEL_UPPER*2 + (1u<<20), Hbytes = 0;
    for (;;){
        std::vector<std::uint8_t> slab1(slabUB);
        Arena measure(slab1.data(), slabUB);
        try { (void)renderFromParams(p, &measure); Hbytes = measure.stats().high_water; break; }
        catch (const std::bad_alloc&){
            // The cap: 4 GiB, or 2 GiB where size_t is 32 bits (WebAssembly), where 1 << 32 would wrap to 0.
            constexpr std::size_t CAP = sizeof(std::size_t) > 4 ? std::size_t(1) << (sizeof(std::size_t) > 4 ? 32 : 0) : std::size_t(1) << 31;
            if (p.model.empty() || slabUB >= CAP / 2){
                std::printf("measure pass overflowed slab - raise PER_PIXEL_UPPER\n"); return 2; }
            slabUB *= 2;
        }
        catch (const raw::assets::AssetError& e){ std::printf("cannot load --model: %s\n", e.what()); return 2; }
    }

    // PASS 2 - render within a budget of EXACTLY the measured footprint.
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena arena(slab2.data(), Hbytes);
    try {
        // Direct initialization elides the extra move. Debug iterator bookkeeping
        // during a vector move can allocate after the measured budget is full.
        const FrameResult o = renderFromParams(p, &arena);
        return emitOutputs(o, p, arena);
    }
    catch (const std::bad_alloc&){
        if (arena.stats().refusals == 0) {
            std::fprintf(stderr, "heap allocation failed; no arena refusal recorded\n");
            return 2;
        }
        Certificate br = certificate_from_arena(arena.stats());
        std::ofstream(out + "/arena_certificate.json") << to_json(br);
        std::printf("arena: %s\n", to_json(br).c_str());     // BREACHED, fail-closed
        return 1;
    }
}
