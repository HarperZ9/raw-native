// The animated lossless WebP loop writer: see raw/core/loop_media.hpp. Container per the WebP
// RIFF specification (VP8X, ANIM, ANMF); frames are VP8L bitstreams (RFC 9649) with the
// subtract-green transform, a colour cache and one prefix-code group, no LZ77 back references.
#include "raw/core/loop_media.hpp"
#include "loop_webp_bits.hpp"
#include <stdexcept>
namespace raw::media {
namespace {
using vp8l::BitWriter;
using vp8l::PrefixCode;
void le(std::string& s, std::uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) s.push_back(char((v >> (8 * i)) & 0xFF)); }
void chunk(std::string& out, const char* tag, const std::string& body) {
    out.append(tag, 4);
    le(out, std::uint32_t(body.size()), 4);
    out += body;
    if (body.size() & 1) out.push_back('\0');
}
std::uint32_t cacheKey(std::uint32_t argb, int bits) { return (0x1e35a7bdu * argb) >> (32 - bits); }

struct Stream { std::vector<std::uint32_t> green, red, blue, alpha, dist; };
// One pass over the pixels: histograms (write == nullptr) or the coded symbols.
void pixels(const std::vector<std::uint32_t>& argb, int cacheBits, Stream* hist, BitWriter* bw, const PrefixCode* codes) {
    std::vector<std::uint32_t> cache(cacheBits ? (std::size_t(1) << cacheBits) : 0, 0);
    for (const std::uint32_t p : argb) {
        if (cacheBits) {
            const std::uint32_t k = cacheKey(p, cacheBits);
            if (cache[k] == p) {
                const int sym = 256 + 24 + int(k);
                if (hist) ++hist->green[std::size_t(sym)]; else vp8l::emit(*bw, codes[0], sym);
                continue;
            }
            cache[k] = p;
        }
        const int a = int(p >> 24), r = int((p >> 16) & 0xFF), g = int((p >> 8) & 0xFF), b = int(p & 0xFF);
        if (hist) { ++hist->green[std::size_t(g)]; ++hist->red[std::size_t(r)]; ++hist->blue[std::size_t(b)]; ++hist->alpha[std::size_t(a)]; continue; }
        vp8l::emit(*bw, codes[0], g); vp8l::emit(*bw, codes[1], r); vp8l::emit(*bw, codes[2], b); vp8l::emit(*bw, codes[3], a);
    }
}
}  // namespace

std::string encodeVp8l(const Frame& rgb, int w, int h, int cacheBits) {
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || rgb.size() != std::size_t(w) * h * 3) throw std::invalid_argument("vp8l: bad image");
    if (cacheBits < 0 || cacheBits > 11) throw std::invalid_argument("vp8l: cache bits outside 0..11");
    std::vector<std::uint32_t> argb(std::size_t(w) * h);
    for (std::size_t i = 0; i < argb.size(); ++i) {   // subtract green
        const std::uint32_t r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        argb[i] = 0xFF000000u | (((r - g) & 0xFF) << 16) | (g << 8) | ((b - g) & 0xFF);
    }
    Stream hist{std::vector<std::uint32_t>(256 + 24 + (cacheBits ? (std::size_t(1) << cacheBits) : 0), 0),
                std::vector<std::uint32_t>(256, 0), std::vector<std::uint32_t>(256, 0), std::vector<std::uint32_t>(256, 0), std::vector<std::uint32_t>(40, 0)};
    pixels(argb, cacheBits, &hist, nullptr, nullptr);
    const PrefixCode codes[5] = {vp8l::buildCode(hist.green), vp8l::buildCode(hist.red), vp8l::buildCode(hist.blue),
                                 vp8l::buildCode(hist.alpha), vp8l::buildCode(hist.dist)};
    BitWriter bw;
    bw.put(0x2f, 8);
    bw.put(std::uint32_t(w - 1), 14); bw.put(std::uint32_t(h - 1), 14);
    bw.put(0, 1); bw.put(0, 3);                  // alpha unused, version 0
    bw.put(1, 1); bw.put(2, 2); bw.put(0, 1);    // one transform: subtract green; then none
    if (cacheBits) { bw.put(1, 1); bw.put(std::uint32_t(cacheBits), 4); } else bw.put(0, 1);
    bw.put(0, 1);                                // no meta prefix codes
    for (const PrefixCode& c : codes) vp8l::writeCode(bw, c);
    pixels(argb, cacheBits, nullptr, &bw, codes);
    bw.flush();
    return bw.bytes;
}

std::string encodeWebp(const std::vector<Frame>& frames, const WebpOptions& o) {
    if (frames.empty() || o.w < 1 || o.h < 1 || o.scale < 1) throw std::invalid_argument("webp: bad options");
    const int W = o.w * o.scale, H = o.h * o.scale;
    std::string body = "WEBP", vp8x, anim;
    vp8x.push_back(0x02); le(vp8x, 0, 3); le(vp8x, std::uint32_t(W - 1), 3); le(vp8x, std::uint32_t(H - 1), 3);
    chunk(body, "VP8X", vp8x);
    le(anim, 0xFFFFFFFFu, 4); le(anim, std::uint32_t(o.loop), 2);
    chunk(body, "ANIM", anim);
    for (std::size_t f = 0; f < frames.size(); ++f) {
        std::string anmf;
        le(anmf, 0, 3); le(anmf, 0, 3); le(anmf, std::uint32_t(W - 1), 3); le(anmf, std::uint32_t(H - 1), 3);
        le(anmf, std::uint32_t(f < o.delaysMs.size() ? o.delaysMs[f] : 83), 3);
        anmf.push_back(0x02);   // do not blend, no disposal
        chunk(anmf, "VP8L", encodeVp8l(upscale(frames[f], o.w, o.h, o.scale), W, H, o.cacheBits));
        chunk(body, "ANMF", anmf);
    }
    std::string out = "RIFF";
    le(out, std::uint32_t(body.size()), 4);
    return out + body;
}

}  // namespace raw::media
