// Loop rendering and loop files for raw_native_cli held-time: see raw/tools/heldtime_cmd.hpp.
#include "raw/tools/heldtime_cmd.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
namespace raw {
namespace {
int maxDiff(const media::Frame& a, const media::Frame& b) {
    int m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return m;
}
}  // namespace

LoopRender renderLoop(const std::string& name, const heldtime::DemoSpec& spec, int threads) {
    const int n = int(spec.loop.frames);
    std::vector<media::Frame> all(std::size_t(n) + 2);
    parallelRows(n + 2, std::max(1, threads), [&](int k) { all[std::size_t(k)] = heldtime::renderDemo(spec, k).rgb; });
    LoopRender r{name, spec.w, spec.h, {}, 0};
    r.closureMaxDiff = std::max(maxDiff(all[std::size_t(n)], all[0]), maxDiff(all[std::size_t(n) + 1], all[1]));
    all.resize(std::size_t(n));
    r.frames = std::move(all);
    return r;
}

LoopRender sideBySide(const std::string& name, const std::vector<const LoopRender*>& parts) {
    if (parts.empty()) throw std::invalid_argument("sideBySide: no parts");
    LoopRender r{name, 0, parts[0]->h, {}, 0};
    for (const LoopRender* p : parts) { r.w += p->w; r.closureMaxDiff = std::max(r.closureMaxDiff, p->closureMaxDiff); }
    for (std::size_t f = 0; f < parts[0]->frames.size(); ++f) {
        media::Frame out(std::size_t(r.w) * r.h * 3);
        int x0 = 0;
        for (const LoopRender* p : parts) {
            for (int y = 0; y < r.h; ++y)
                std::copy_n(p->frames[f].begin() + std::ptrdiff_t(std::size_t(y) * p->w * 3), std::size_t(p->w) * 3,
                            out.begin() + std::ptrdiff_t((std::size_t(y) * r.w + std::size_t(x0)) * 3));
            x0 += p->w;
        }
        r.frames.push_back(std::move(out));
    }
    return r;
}

LoopFiles writeLoop(const LoopRender& r, const std::string& out, const std::string& raw, std::int64_t fpsNum, std::int64_t fpsDen,
                    int scale, int paletteColours) {
    LoopFiles lf;
    const std::string stem = out + "/" + r.name + (scale > 1 ? "-x" + std::to_string(scale) : "");
    const std::vector<media::Rgb8> pal = media::medianCut(r.frames, paletteColours);
    std::vector<std::vector<std::uint8_t>> idx;
    for (const media::Frame& f : r.frames) idx.push_back(media::mapNearest(f, pal));
    media::GifOptions go; go.w = r.w; go.h = r.h; go.scale = scale;
    go.delaysCs = media::frameDelays(int(r.frames.size()), fpsNum, fpsDen, 100);
    const std::string gif = media::encodeGif(pal, idx, go);
    media::WebpOptions wo; wo.w = r.w; wo.h = r.h; wo.scale = scale;
    wo.delaysMs = media::frameDelays(int(r.frames.size()), fpsNum, fpsDen, 1000);
    const std::string webp = media::encodeWebp(r.frames, wo);
    lf.gif = stem + ".gif"; lf.webp = stem + ".webp";
    if (!media::writeFile(lf.gif, gif) || !media::writeFile(lf.webp, webp)) throw std::runtime_error("held-time: cannot write " + stem);
    lf.gifBytes = (long long)gif.size(); lf.webpBytes = (long long)webp.size(); lf.paletteSize = int(pal.size());
    media::GifImage g; std::string err;
    if (!media::decodeGif(gif, g, err)) throw std::runtime_error("held-time: own GIF did not decode: " + err);
    lf.offPalette = 0; lf.gifExact = g.frames.size() == idx.size();
    for (std::size_t f = 0; f < g.frames.size() && f < idx.size(); ++f) {
        for (const std::uint8_t v : g.frames[f]) lf.offPalette += v >= pal.size();
        lf.gifExact = lf.gifExact && g.frames[f] == media::upscale(idx[f], r.w, r.h, scale, 1);
    }
    if (!raw.empty() && scale == 1) {
        std::filesystem::create_directories(raw);
        lf.raw = raw + "/" + r.name + ".rgb";
        std::string bytes;
        for (const media::Frame& f : r.frames) bytes.append(reinterpret_cast<const char*>(f.data()), f.size());
        if (!media::writeFile(lf.raw, bytes)) throw std::runtime_error("held-time: cannot write " + lf.raw);
    }
    return lf;
}

}  // namespace raw
