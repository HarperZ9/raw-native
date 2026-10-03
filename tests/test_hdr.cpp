// HDR radiance channel: linear radiance before tonemap, plus Reinhard.
#include "raw/composite.hpp"
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
#include "check.hpp"
#include <cmath>
using namespace raw;
int main() {
    // (1) Reinhard scalar properties: maps 0 -> 0, stays in [0,1), monotonic.
    CHECK_NEAR(tonemapReinhard(0.0f), 0.0, 1e-9);
    float prev = -1.0f;
    for (float L = 0.0f; L <= 100.0f; L += 0.5f){
        float d = tonemapReinhard(L);
        CHECK(d >= 0.0f && d < 1.0f);        // never reaches or exceeds 1
        CHECK(d >= prev);                    // monotonic non-decreasing
        prev = d;
    }
    CHECK(tonemapReinhard(1.0f) > tonemapReinhard(0.5f));   // strictly rising on data

    // (2) Build a synthetic 1x1 covered GBuffer whose lighting exceeds 1.0.
    //     radiance = albedo * (ambient + N.L * intensity) * ao. With albedo 2.0,
    //     N aligned to the light, intensity 5, ao 1 -> well above 1.0.
    GBuffer g; g.resize(1,1);
    g.mask.at(0,0) = 1;
    g.normal.at(0,0) = {0,1,0};
    g.albedo.at(0,0) = {2.0f, 2.0f, 2.0f};
    g.position.at(0,0) = {0,0,0};
    Buffer<float> ao; ao.resize(1,1); ao.at(0,0) = 1.0f;
    Scene s;
    s.lights.push_back(Light{ Vec3{0,-1,0}, 5.0f });   // points down; -dir = up = N

    Buffer<Vec3> hdr = shadeHDR(g, ao, s);
    Vec3 r = hdr.at(0,0);
    // HDR survives UNCLAMPED above 1.0
    CHECK(r.x > 1.0f);
    CHECK_NEAR(r.x, 2.0f * (0.2f + 1.0f * 5.0f) * 1.0f, 1e-4);   // = 2 * 5.2 = 10.4
    float head = maxRadiance(hdr);
    CHECK_NEAR(head, r.x, 1e-6);
    CHECK(head > 1.0f);

    // (3) Tonemapped output is within [0,1] and leaves the HDR buffer intact.
    Buffer<Vec3> disp = tonemapReinhard(hdr);
    CHECK(disp.at(0,0).x >= 0.0f && disp.at(0,0).x <= 1.0f);
    CHECK(hdr.at(0,0).x > 1.0f);              // original HDR untouched by tonemap
    CHECK_NEAR(disp.at(0,0).x, r.x / (1.0f + r.x), 1e-5);

    // (4) The displayable shade() path stays clamped to [0,1] (human view), while
    //     the HDR path on the SAME inputs is larger -> the model sees more.
    Buffer<Vec3> ldr = shade(g, ao, s);
    CHECK(ldr.at(0,0).x <= 1.0f);
    CHECK(hdr.at(0,0).x > ldr.at(0,0).x);
    return raw_test_summary();
}
