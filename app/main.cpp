#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/render.hpp"
#include "raw/mat.hpp"
#include "raw/arena.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <optional>
#include <utility>
using namespace raw;
// Render one frame and every Tier-3 channel. prevViewProj defaults to the
// current frame's view-projection (static camera -> zero motion).
static FrameResult renderFrame(int W, int H, Arena* arena){
    Scene s = buildTestScene(W, H, arena);
    Mat4 curVP = mul(s.camera.proj(), s.camera.view());
    return renderWithParams(s, W, H, /*prevViewProj=*/curVP, arena);
}
int main(int argc, char** argv){
    std::string out = argc > 1 ? argv[1] : ".";
    const int W = 256, H = 256;

    // PASS 1 - MEASURE the footprint in a computed generous slab (no magic constant).
    const std::size_t PER_PIXEL_UPPER =
        sizeof(float) + 3*sizeof(Vec3) + sizeof(std::uint8_t)   // gbuffer: depth+nrm/pos/alb+mask
        + sizeof(Vec2)                                          // gbuffer: motion plane (Tier-3)
        + 3*sizeof(float) + 2*sizeof(Vec3);                     // aoRT + aoSS + errorMap + frame + hdr
    std::size_t slabUB = (std::size_t)W*H*PER_PIXEL_UPPER*2 + (1u<<20);
    std::vector<std::uint8_t> slab1(slabUB);
    Arena measure(slab1.data(), slabUB);
    try { (void)renderFrame(W, H, &measure); }
    catch (const std::bad_alloc&){ std::printf("measure pass overflowed slab - raise PER_PIXEL_UPPER\n"); return 2; }
    std::size_t Hbytes = measure.stats().high_water;

    // PASS 2 - render within a budget of EXACTLY the measured footprint.
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena arena(slab2.data(), Hbytes);
    FrameResult o(&arena);
    try { o = renderFrame(W, H, &arena); }
    catch (const std::bad_alloc&){
        Certificate br = certificate_from_arena(arena.stats());
        std::ofstream(out + "/arena_certificate.json") << to_json(br);
        std::printf("arena: %s\n", to_json(br).c_str());     // BREACHED, fail-closed
        return 1;
    }
    writePPM(o.frame, out + "/frame.ppm");           // human view: tonemapped/clamped 8-bit
    writePFM(o.hdr,   out + "/frame_hdr.pfm");        // model view: exact linear radiance
    writePGM(o.aoRT,  out + "/ao_rt.pgm");
    writePGM(o.aoSS,  out + "/ao_ss.pgm");
    writePGM(o.rec.errorMap, out + "/ao_error.pgm");

    // Per-channel fidelity witness. Static-camera CLI -> motion is all-zero but
    // fully valid, so motion coherence is 1.0; HDR headroom is the real max.
    std::optional<double> motionCoherence =
        o.motionTotal > 0 ? std::optional<double>((double)o.motionValid / o.motionTotal)
                          : std::nullopt;
    std::optional<double> hdrHeadroom = std::optional<double>(maxRadiance(o.hdr));
    Certificate aoCert    = certificate_with_channels(o.rec, 0.12f, motionCoherence, hdrHeadroom);
    Certificate arenaCert = certificate_from_arena(arena.stats());
    std::ofstream(out + "/certificate.json")        << to_json(aoCert);
    std::ofstream(out + "/arena_certificate.json")  << to_json(arenaCert);

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
