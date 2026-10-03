#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/render.hpp"
#include "raw/cli_params.hpp"
#include "raw/channels_json.hpp"
#include "raw/mat.hpp"
#include "raw/arena.hpp"
#include "raw/version.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <optional>
#include <utility>
#include <cstring>
using namespace raw;
// Build the parameterized scene: the canonical test geometry/lights, but the
// camera and frame size come from the CLI params (the model's chosen view).
static Scene buildScene(const CliParams& p, Arena* arena){
    Scene s = buildTestScene(p.width, p.height, arena);
    s.camera = cameraFromParams(p);
    return s;
}
// Render one frame and every extra channel for the given params. The previous
// camera (when supplied) drives motion reprojection; otherwise the previous
// view-projection equals the current one -> honest zero motion.
static FrameResult renderFrame(const CliParams& p, Arena* arena){
    Scene s = buildScene(p, arena);
    Mat4 curVP  = mul(s.camera.proj(), s.camera.view());
    Mat4 prevVP = curVP;
    if (p.hasPrevCamera()){
        Camera pc = prevCameraFromParams(p);
        prevVP = mul(pc.proj(), pc.view());
    }
    return renderWithParams(s, p.width, p.height, prevVP, arena);
}
// Write every output and print the summary for a frame rendered inside `arena`.
static int emitOutputs(const FrameResult& o, const CliParams& p, Arena& arena){
    const std::string out = p.out;
    writePPM(o.frame, out + "/frame.ppm");           // human view: tonemapped/clamped 8-bit
    writePFM(o.hdr,   out + "/frame_hdr.pfm");        // model view: exact linear radiance
    writePGM(o.aoRT,  out + "/ao_rt.pgm");
    writePGM(o.aoSS,  out + "/ao_ss.pgm");
    writePGM(o.rec.errorMap, out + "/ao_error.pgm");

    // Per-channel fidelity witness. Motion coherence is null when no covered
    // pixel has a denominator; with a static (or absent) previous camera every
    // covered pixel is a valid zero, so coherence is 1.0. HDR headroom is real.
    std::optional<double> motionCoherence =
        o.motionTotal > 0 ? std::optional<double>((double)o.motionValid / o.motionTotal)
                          : std::nullopt;
    std::optional<double> hdrHeadroom = std::optional<double>(maxRadiance(o.hdr));
    Certificate aoCert    = certificate_with_channels(o.rec, 0.12f, motionCoherence, hdrHeadroom);
    Certificate arenaCert = certificate_from_arena(arena.stats());
    std::ofstream(out + "/certificate.json")        << to_json(aoCert);
    std::ofstream(out + "/arena_certificate.json")  << to_json(arenaCert);

    // The single machine-readable artifact the two-way loop reads back: the
    // certificate PLUS compact channel summaries, so a caller need not parse the
    // raw image files. Absent channels are honest null.
    std::ofstream(out + "/channels.json") << channelsJson(o, p);

    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        o.rec.pixels, o.rec.rmse, o.rec.maxError,
        o.rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT");
    std::printf("channels: ao_fidelity=%.4f motion_coherence=%.4f (%d/%d) hdr_headroom=%.4f\n",
        aoFidelityFromRmse(o.rec.rmse),
        motionCoherence.has_value() ? *motionCoherence : 0.0,
        o.motionValid, o.motionTotal, hdrHeadroom.has_value() ? *hdrHeadroom : 0.0);
    std::printf("certificate: %s\n", to_json(aoCert).c_str());
    std::printf("arena: %s\n", arena_witness(arena.stats()).c_str());
    std::printf("arena-certificate: %s\n", to_json(arenaCert).c_str());
    return 0;
}
static const char* kUsage =
    "usage: raw_native_cli [outdir] [flags]\n"
    "  --out <dir>                 output directory (default .)\n"
    "  --width <int> --height <int> frame size (default 256x256)\n"
    "  --eye x,y,z --target x,y,z --up x,y,z   camera\n"
    "  --fovy <radians>            vertical field of view (default 0.9)\n"
    "  --prev-eye x,y,z --prev-target x,y,z --prev-up x,y,z   previous camera for motion\n"
    "  --params <file.json>        load parameters first; later flags override\n"
    "  --version                   print the version and exit\n"
    "  --help                      print this text and exit\n"
    "exit codes: 0 rendered, 1 memory budget breached (fail-closed), 2 bad input or setup\n";
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
    std::string err;
    std::optional<CliParams> parsed = parseArgs(argc, argv, err);
    if (!parsed){ std::printf("param error: %s\n", err.c_str()); return 2; }
    const CliParams p = *parsed;
    const std::string out = p.out;
    const int W = p.width, H = p.height;

    // PASS 1 - MEASURE the footprint in a computed generous slab (no magic constant).
    const std::size_t PER_PIXEL_UPPER =
        sizeof(float) + 3*sizeof(Vec3) + sizeof(std::uint8_t)   // gbuffer: depth+nrm/pos/alb+mask
        + sizeof(Vec2)                                          // gbuffer: motion plane
        + 3*sizeof(float) + 2*sizeof(Vec3);                     // aoRT + aoSS + errorMap + frame + hdr
    std::size_t slabUB = (std::size_t)W*H*PER_PIXEL_UPPER*2 + (1u<<20);
    std::vector<std::uint8_t> slab1(slabUB);
    Arena measure(slab1.data(), slabUB);
    try { (void)renderFrame(p, &measure); }
    catch (const std::bad_alloc&){ std::printf("measure pass overflowed slab - raise PER_PIXEL_UPPER\n"); return 2; }
    std::size_t Hbytes = measure.stats().high_water;

    // PASS 2 - render within a budget of EXACTLY the measured footprint.
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena arena(slab2.data(), Hbytes);
    try {
        // Direct initialization elides the extra move. Debug iterator bookkeeping
        // during a vector move can allocate after the measured budget is full.
        const FrameResult o = renderFrame(p, &arena);
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
