// Palette, scaling, delays and file output for the loop writers: see raw/core/loop_media.hpp.
#include "raw/core/loop_media.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
namespace raw::media {
namespace {
struct Entry { std::uint32_t rgb; std::uint64_t n; };
std::uint32_t pack(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2]; }
int chan(std::uint32_t c, int k) { return int((c >> (16 - 8 * k)) & 0xFF); }

struct Box { std::size_t a, b; };   // [a, b) into the entry list
int widestChannel(const std::vector<Entry>& e, const Box& bx, int& range) {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (std::size_t i = bx.a; i < bx.b; ++i) for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], chan(e[i].rgb, k)); hi[k] = std::max(hi[k], chan(e[i].rgb, k)); }
    int best = 0; range = -1;
    for (int k = 0; k < 3; ++k) if (hi[k] - lo[k] > range) { range = hi[k] - lo[k]; best = k; }
    return best;
}
Rgb8 mean(const std::vector<Entry>& e, const Box& bx) {
    double s[3] = {0, 0, 0}, n = 0;
    for (std::size_t i = bx.a; i < bx.b; ++i) { for (int k = 0; k < 3; ++k) s[k] += double(chan(e[i].rgb, k)) * double(e[i].n); n += double(e[i].n); }
    Rgb8 c{};
    for (int k = 0; k < 3; ++k) c[std::size_t(k)] = std::uint8_t(std::lround(s[k] / n));
    return c;
}
}  // namespace

std::vector<Rgb8> medianCut(const std::vector<Frame>& frames, int maxColours) {
    if (maxColours < 1 || maxColours > 256) throw std::invalid_argument("palette: maxColours outside 1..256");
    std::unordered_map<std::uint32_t, std::uint64_t> count;
    for (const Frame& f : frames) for (std::size_t i = 0; i + 2 < f.size(); i += 3) ++count[pack(&f[i])];
    std::vector<Entry> e;
    for (const auto& [c, n] : count) e.push_back({c, n});
    std::sort(e.begin(), e.end(), [](const Entry& x, const Entry& y) { return x.rgb < y.rgb; });
    std::vector<Box> boxes{{0, e.size()}};
    while (int(boxes.size()) < maxColours) {
        int pick = -1, bestRange = 0, axis = 0;
        for (std::size_t j = 0; j < boxes.size(); ++j) {
            if (boxes[j].b - boxes[j].a < 2) continue;
            int r = 0; const int k = widestChannel(e, boxes[j], r);
            if (r > bestRange) { bestRange = r; pick = int(j); axis = k; }
        }
        if (pick < 0) break;
        Box bx = boxes[std::size_t(pick)];
        std::sort(e.begin() + std::ptrdiff_t(bx.a), e.begin() + std::ptrdiff_t(bx.b), [axis](const Entry& x, const Entry& y) {
            return chan(x.rgb, axis) != chan(y.rgb, axis) ? chan(x.rgb, axis) < chan(y.rgb, axis) : x.rgb < y.rgb; });
        std::uint64_t total = 0, run = 0;
        for (std::size_t i = bx.a; i < bx.b; ++i) total += e[i].n;
        std::size_t cut = bx.a + 1;
        for (std::size_t i = bx.a; i + 1 < bx.b; ++i) { run += e[i].n; cut = i + 1; if (2 * run >= total) break; }
        boxes[std::size_t(pick)] = {bx.a, cut};
        boxes.push_back({cut, bx.b});
    }
    std::vector<Rgb8> pal;
    for (const Box& bx : boxes) if (bx.b > bx.a) pal.push_back(mean(e, bx));
    return pal;
}

std::vector<std::uint8_t> mapNearest(const Frame& rgb, const std::vector<Rgb8>& palette) {
    if (palette.empty() || palette.size() > 256) throw std::invalid_argument("palette: size outside 1..256");
    std::unordered_map<std::uint32_t, std::uint8_t> memo;
    std::vector<std::uint8_t> out(rgb.size() / 3);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint32_t c = pack(&rgb[i * 3]);
        auto it = memo.find(c);
        if (it == memo.end()) {
            int best = 0; long bestD = -1;
            for (std::size_t j = 0; j < palette.size(); ++j) {
                long d = 0;
                for (int k = 0; k < 3; ++k) { const long t = long(rgb[i * 3 + std::size_t(k)]) - long(palette[j][std::size_t(k)]); d += t * t; }
                if (bestD < 0 || d < bestD) { bestD = d; best = int(j); }
            }
            it = memo.emplace(c, std::uint8_t(best)).first;
        }
        out[i] = it->second;
    }
    return out;
}

Frame upscale(const Frame& rgb, int w, int h, int k, int channels) {
    if (k < 1) throw std::invalid_argument("upscale: scale must be a whole number >= 1");
    if (k == 1) return rgb;
    Frame out(std::size_t(w) * k * std::size_t(h) * k * std::size_t(channels));
    const std::size_t W = std::size_t(w) * k;
    for (std::size_t y = 0; y < std::size_t(h) * k; ++y) for (std::size_t x = 0; x < W; ++x)
        for (int c = 0; c < channels; ++c) out[(y * W + x) * channels + c] = rgb[((y / k) * w + x / k) * channels + c];
    return out;
}

std::vector<int> frameDelays(int frames, std::int64_t fpsNum, std::int64_t fpsDen, int unitsPerSecond) {
    if (fpsNum <= 0 || fpsDen <= 0) throw std::invalid_argument("delays: bad fps");
    std::vector<int> d;
    auto at = [&](std::int64_t i) { return (2 * i * unitsPerSecond * fpsDen + fpsNum) / (2 * fpsNum); };   // round half up
    for (int i = 0; i < frames; ++i) d.push_back(int(at(i + 1) - at(i)));
    return d;
}

bool writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), std::streamsize(bytes.size()));
    return bool(f);
}

}  // namespace raw::media
