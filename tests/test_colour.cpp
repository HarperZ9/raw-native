// The colour reference (raw/renderer/colour.hpp): matrices, transfer functions,
// tone mappers and pipelines. Agreement with OpenColorIO 2.6.0 is measured
// offline by tools/colour/ocio_compare.py (evidence/m1-colour-ocio.json).
#include "raw/renderer/colour.hpp"
#include "check.hpp"
#include <cmath>
using namespace raw::colour;

static void matrices(){
    const Mat3 m = rgbToXyz(kRec709);                 // the sRGB matrix of IEC 61966-2-1
    CHECK_NEAR(m[0], 0.4123908, 1e-6); CHECK_NEAR(m[1], 0.3575843, 1e-6); CHECK_NEAR(m[4], 0.7151687, 1e-6);
    CHECK_NEAR(m[3] + m[4] + m[5], 1.0, 1e-12);        // white has Y = 1
    const Mat3 id = multiply(m, inverse(m));
    for (int i = 0; i < 9; ++i) CHECK_NEAR(id[i], i % 4 == 0 ? 1.0 : 0.0, 1e-12);
    const RGB w = apply(conversion(kRec709, kAP0), {1, 1, 1});   // Bradford keeps white white
    CHECK_NEAR(w[0], 1.0, 1e-6); CHECK_NEAR(w[1], 1.0, 1e-6); CHECK_NEAR(w[2], 1.0, 1e-6);
    const RGB p = apply(conversion(kRec709, kP3D65), {1, 0, 0});
    CHECK(p[0] < 1.0f && p[1] > 0.0f);                 // Rec.709 red sits inside P3
}

static void transfers(){
    CHECK_NEAR(srgbEncode(0.18), 0.4613561, 1e-6);
    CHECK_NEAR(srgbEncode(0.002), 0.02584, 1e-6);      // the linear segment
    CHECK_NEAR(pqEncode(1.0), 0.5080784, 1e-6);        // 100 nits
    CHECK_NEAR(pqEncode(100.0), 1.0, 1e-9);            // 10,000 nits
    for (double v = 0.0; v <= 1.0; v += 0.0625){
        CHECK_NEAR(srgbDecode(srgbEncode(v)), v, 1e-12);
        CHECK_NEAR(bt1886Decode(bt1886Encode(v)), v, 1e-12);
        CHECK_NEAR(pqDecode(pqEncode(v * 100)), v * 100, 1e-6 * (1 + v * 100));
    }
}

static void toneMappers(){
    const RGB a = pbrNeutral({0.5f, 0.3f, 0.2f});      // below the knee: minus the 0.04 offset
    CHECK_NEAR(a[0], 0.46, 1e-6); CHECK_NEAR(a[1], 0.26, 1e-6); CHECK_NEAR(a[2], 0.16, 1e-6);
    const RGB b = pbrNeutral({1000, 1000, 1000});      // white compresses toward 1
    CHECK(b[0] < 1.0f && b[0] > 0.99f);
    float prevP = -1, prevA = -1;
    for (int k = 0; k < 64; ++k){                     // grey stays grey and rises
        const float v = std::exp2(-10.0f + k * 0.25f);
        const RGB p = pbrNeutral({v, v, v}), x = agx({v, v, v});
        CHECK(p[0] >= prevP && x[0] >= prevA);
        CHECK(std::fabs(x[0] - x[1]) < 1e-4f && std::fabs(x[1] - x[2]) < 1e-4f);
        prevP = p[0]; prevA = x[0];
    }
}

static void pipelines(){
    CHECK(pipelineNames().size() == 10);
    Pipeline p;
    CHECK(!parsePipeline("aces2-sdr/rec2100-pq", p));
    CHECK(parsePipeline("aces2-sdr/srgb", p) && p.tone == Tone::Aces2 && p.peakNits == 100.0f);
    const Transform sdr(p);
    // ACES 2.0 puts scene 18% grey at about 10 nits on a 100-nit display.
    const RGB g = sdr.apply({0.18f, 0.18f, 0.18f});
    CHECK_NEAR(srgbDecode(g[0]), 0.10013, 2e-3);
    CHECK_NEAR(g[0], g[1], 1e-4); CHECK_NEAR(g[1], g[2], 1e-4);
    CHECK(sdr.apply({0, 0, 0})[0] == 0.0f);
    CHECK(sdr.apply({1e4f, 1e4f, 1e4f})[0] <= 1.0f);
    CHECK(parsePipeline("aces2-hdr1000/rec2100-pq", p));
    const Transform hdr(p);
    const RGB top = hdr.apply({1e4f, 1e4f, 1e4f});
    CHECK(top[0] <= pqEncode(10.0) + 1e-6 && top[0] > pqEncode(9.0));   // peaks near 1,000 nits
    CHECK(sdr.aces2Json().find("\"cusp\":[") != std::string::npos);
    CHECK(parsePipeline("clip/srgb", p) && Transform(p).aces2Json() == "null");
}

static void gridShape(){
    const auto g = grid();
    CHECK(g.size() == 33u * 33 * 33 + 256 + 594);
    CHECK(g[0][0] == 0.0f && g[32][0] == 128.0f && g[33][1] == 0.0009765625f);
    CHECK(g[33 * 33 * 33][0] == std::exp2(-12.0f));
    CHECK(g.back()[0] == 8.0f && g.back()[1] == 0.0f && g.back()[2] == 8.0f);   // magenta at L = 8
}

int main(){
    matrices();
    transfers();
    toneMappers();
    pipelines();
    gridShape();
    return raw_test_summary();
}
