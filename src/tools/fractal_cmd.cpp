// raw_native_cli fractal-dither: see raw/tools/fractal_cmd.hpp.
#include "raw/tools/fractal_cmd.hpp"
#include "raw/renderer/fractal_metrics.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
namespace raw {
namespace fractal {

int scaleReport(std::string& json) {
    const int w = 320, h = 180;
    const double depths[5] = {4, 5.7, 8, 11.3, 16};
    double lo = 1e9, hi = 0, loB = 1e9, hiB = 0;
    json = "{\n \"check\": \"fractal dither scale invariance (f3)\", \"depths\": [4, 5.7, 8, 11.3, 16],\n \"density\": [";
    std::string broken;
    for (int variant = 0; variant < 2; ++variant) {
        for (std::size_t k = 0; k < 5; ++k) {
            style::DitherPlan p = orbitPlan("ink", style::Anchor::Fractal, h, 2.0);
            if (variant) p.fractalDepth = 8.0;
            style::DitherGBuffer g;
            g.w = w; g.h = h;
            const std::size_t n = std::size_t(w) * std::size_t(h);
            g.albedo.assign(n, 0.5f); g.depth.assign(n, float(depths[k])); g.kind.assign(n, 1);
            for (auto* v : {&g.wpos, &g.nrm, &g.apos, &g.anrm}) v->assign(n * 3, 0.0f);
            g.vis.assign(n * 4, 0.0f);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
                    g.apos[i * 3] = float((x + 0.5 - w / 2.0) * p.pa * depths[k]);
                    g.apos[i * 3 + 1] = float(-(y + 0.5 - h / 2.0) * p.pa * depths[k]);
                    g.apos[i * 3 + 2] = float(-depths[k]);
                    g.anrm[i * 3 + 2] = 1.0f;
                }
            const style::DitherFrame f = style::runDitherlight(p, {}, g);
            const double d = changeDensity(f.level, w, h);
            char b[48];
            std::snprintf(b, sizeof b, "%s%.4f", k ? ", " : "", d);
            (variant ? broken : json) += b;
            (variant ? loB : lo) = std::min(variant ? loB : lo, d);
            (variant ? hiB : hi) = std::max(variant ? hiB : hi, d);
        }
    }
    const double ratio = hi / lo, ratioB = hiB / loB;
    const bool ok = ratio <= 1.5 && ratioB > 1.5;
    char tail[512];
    std::snprintf(tail, sizeof tail, "],\n \"ratio_max_over_min\": %.3f, \"bound\": 1.5,\n \"control_scale_term_broken\": {\"density\": [%s], \"ratio\": %.3f, \"fails_as_required\": %s},\n \"pass\": %s\n}\n",
                  ratio, broken.c_str(), ratioB, ratioB > 1.5 ? "true" : "false", ok ? "true" : "false");
    json += tail;
    return ok ? 0 : 1;
}

}  // namespace fractal

namespace {
int images(const std::string& dir, int w, int h) {
    std::filesystem::create_directories(dir);
    const std::vector<fractal::Frame> fr = fractal::orbitFrames(25, w, h);
    for (int t : {0, 24})
        for (style::Anchor a : {style::Anchor::Screen, style::Anchor::WorldOctave, style::Anchor::Fractal}) {
            const style::DitherPlan p = fractal::orbitPlan("ink", a, h, 2.0);
            const style::DitherFrame f = style::runDitherlight(p, fractal::lights(), fr[std::size_t(t)].g);
            std::vector<std::uint8_t> rgb(f.rgba.size() / 4 * 3);
            for (std::size_t i = 0; i < rgb.size() / 3; ++i) for (std::size_t c = 0; c < 3; ++c) rgb[i * 3 + c] = f.rgba[i * 4 + c];
            const char* names[] = {"screen", "world-fixed", "world-octave", "fractal"};
            writePng(dir + "/frame" + std::to_string(t) + "-" + names[std::size_t(a)] + ".png", w, h, rgb);
        }
    std::printf("fractal-dither: images in %s\n", dir.c_str());
    return 0;
}
}  // namespace

int fractalCommand(int argc, char** argv) {
    const char* usage = "usage: raw_native_cli fractal-dither orbit|scale|images|gpu [--out FILE|DIR] [--frames N] [--px P] [--width W] [--height H] [--cost]\n";
    if (argc < 2) { std::printf("%s", usage); return 2; }
    const std::string sub = argv[1];
    std::string out;
    int frames = 48, w = 320, h = 180;
    double px = 2.0;
    bool cost = false;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--px") && i + 1 < argc) px = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) h = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cost")) cost = true;
        else { std::printf("%s", usage); return 2; }
    }
    if (frames < 2 || w < 16 || h < 16 || px <= 0) { std::printf("fractal-dither: frames at least 2, sizes at least 16, px positive\n"); return 2; }
    if (sub == "images") return images(out.empty() ? "fractal-dither" : out, w, h);
    std::string json;
    int rc;
    if (sub == "orbit") rc = fractal::orbitReport(frames, w, h, px, json);
    else if (sub == "scale") rc = fractal::scaleReport(json);
    else if (sub == "gpu") rc = fractal::gpuReport(cost, json);
    else { std::printf("%s", usage); return 2; }
    std::fputs(json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << json;
    return rc;
}
}  // namespace raw
