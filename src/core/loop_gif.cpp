// The GIF89a loop writer: see raw/core/loop_media.hpp. LZW follows the GIF89a specification
// (appendix F, variable-length codes, clear at 4096 entries).
#include "raw/core/loop_media.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
namespace raw::media {
namespace {
struct Bits {
    std::string bytes; std::uint32_t acc{0}; int n{0};
    void put(std::uint32_t code, int size) {
        acc |= code << n; n += size;
        while (n >= 8) { bytes.push_back(char(acc & 0xFF)); acc >>= 8; n -= 8; }
    }
    void flush() { if (n > 0) bytes.push_back(char(acc & 0xFF)); acc = 0; n = 0; }
};
void u16(std::string& s, int v) { s.push_back(char(v & 0xFF)); s.push_back(char((v >> 8) & 0xFF)); }

std::string lzw(const std::vector<std::uint8_t>& px, int minSize) {
    const std::uint32_t clear = 1u << minSize, eoi = clear + 1;
    std::unordered_map<std::uint32_t, std::uint32_t> dict;
    Bits b;
    int size = minSize + 1;
    std::uint32_t last = eoi;
    b.put(clear, size);
    std::int64_t cur = -1;
    for (const std::uint8_t v : px) {
        if (cur < 0) { cur = v; continue; }
        const std::uint32_t key = (std::uint32_t(cur) << 8) | v;
        if (auto it = dict.find(key); it != dict.end()) { cur = it->second; continue; }
        b.put(std::uint32_t(cur), size);
        dict[key] = ++last;
        if (last >= (1u << size) && size < 12) ++size;
        if (last == 4095) { b.put(clear, size); dict.clear(); size = minSize + 1; last = eoi; }
        cur = v;
    }
    if (cur >= 0) {
        b.put(std::uint32_t(cur), size);
        if (last + 1 >= (1u << size) && size < 12) ++size;   // the decoder adds an entry after this code
    }
    b.put(eoi, size);
    b.flush();
    std::string out(1, char(minSize));
    for (std::size_t i = 0; i < b.bytes.size(); i += 255) {
        const std::size_t n = std::min<std::size_t>(255, b.bytes.size() - i);
        out.push_back(char(n));
        out.append(b.bytes, i, n);
    }
    out.push_back('\0');
    return out;
}

struct Rect { int x0, y0, x1, y1; };
Rect changed(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int w, int h) {
    Rect r{w, h, 0, 0};
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + x;
        if (a[i] != b[i]) { r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y); r.x1 = std::max(r.x1, x + 1); r.y1 = std::max(r.y1, y + 1); }
    }
    if (r.x1 == 0) r = {0, 0, 1, 1};   // nothing changed: one transparent pixel keeps the frame's time
    return r;
}

void frameBlock(std::string& out, const std::vector<std::uint8_t>& sub, int x, int y, int w, int h, int delay, int trans, int minSize) {
    out += "\x21\xF9\x04";
    out.push_back(char((1 << 2) | (trans >= 0 ? 1 : 0)));   // disposal 1: leave in place
    u16(out, delay);
    out.push_back(char(trans >= 0 ? trans : 0));
    out.push_back('\0');
    out.push_back(',');
    u16(out, x); u16(out, y); u16(out, w); u16(out, h);
    out.push_back('\0');
    out += lzw(sub, minSize);
}
}  // namespace

std::string encodeGif(const std::vector<Rgb8>& palette, const std::vector<std::vector<std::uint8_t>>& indices, const GifOptions& o) {
    if (palette.empty() || palette.size() > 256 || o.w <= 0 || o.h <= 0 || o.scale < 1 || indices.empty()) throw std::invalid_argument("gif: bad options");
    if (o.w * o.scale > 65535 || o.h * o.scale > 65535) throw std::invalid_argument("gif: too large");
    const bool delta = o.delta && palette.size() < 256;
    const int trans = delta ? int(palette.size()) : -1;
    int depth = 1;
    while ((1 << depth) < int(palette.size()) + (delta ? 1 : 0)) ++depth;
    const int minSize = std::max(2, depth), k = o.scale, W = o.w * k, H = o.h * k;
    std::string out = "GIF89a";
    u16(out, W); u16(out, H);
    out.push_back(char(0x80 | ((depth - 1) << 4) | (depth - 1)));
    out.push_back('\0'); out.push_back('\0');
    for (int i = 0; i < (1 << depth); ++i) for (int c = 0; c < 3; ++c) out.push_back(char(i < int(palette.size()) ? palette[std::size_t(i)][std::size_t(c)] : 0));
    out += "\x21\xFF\x0BNETSCAPE2.0\x03\x01";
    u16(out, o.loop);
    out.push_back('\0');
    for (std::size_t f = 0; f < indices.size(); ++f) {
        const std::vector<std::uint8_t>& cur = indices[f];
        if (cur.size() != std::size_t(o.w) * o.h) throw std::invalid_argument("gif: frame size");
        const int delay = f < o.delaysCs.size() ? o.delaysCs[f] : 8;
        Rect r{0, 0, o.w, o.h};
        if (delta && f > 0) r = changed(indices[f - 1], cur, o.w, o.h);
        const int rw = r.x1 - r.x0, rh = r.y1 - r.y0;
        std::vector<std::uint8_t> sub(std::size_t(rw) * rh);
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
            const std::size_t i = std::size_t(r.y0 + y) * o.w + std::size_t(r.x0 + x);
            const bool same = delta && f > 0 && cur[i] == indices[f - 1][i];
            sub[std::size_t(y) * rw + x] = same ? std::uint8_t(trans) : cur[i];
        }
        frameBlock(out, upscale(sub, rw, rh, k, 1), r.x0 * k, r.y0 * k, rw * k, rh * k, delay, (delta && f > 0) ? trans : -1, minSize);
    }
    out.push_back(';');
    return out;
}

}  // namespace raw::media
