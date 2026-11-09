// A GIF decoder for what encodeGif writes (raw/core/loop_media.hpp): global colour table, graphic
// control blocks, NETSCAPE2.0 loop, no interlace, disposal "none" or "leave in place". The tests and
// the loop tool read GIFs back through it, so a palette or closure check runs on decoded pixels.
#include "raw/core/loop_media.hpp"
#include <vector>
namespace raw::media {
namespace {
struct Reader {
    const std::string& s; std::size_t p{0}; bool bad{false};
    int u8() { if (p >= s.size()) { bad = true; return 0; } return int(static_cast<unsigned char>(s[p++])); }
    int u16() { const int a = u8(); return a | (u8() << 8); }
    std::string blocks() { std::string out; for (int n = u8(); n > 0 && !bad; n = u8()) { if (p + std::size_t(n) > s.size()) { bad = true; break; } out.append(s, p, std::size_t(n)); p += std::size_t(n); } return out; }
};

bool unlzw(const std::string& data, int minSize, std::size_t want, std::vector<std::uint8_t>& out) {
    const int clear = 1 << minSize, eoi = clear + 1;
    std::vector<std::vector<std::uint8_t>> dict;
    auto reset = [&] { dict.assign(std::size_t(eoi) + 1, {}); for (int i = 0; i < clear; ++i) dict[std::size_t(i)] = {std::uint8_t(i)}; };
    reset();
    int size = minSize + 1, prev = -1;
    std::size_t bit = 0;
    while (out.size() < want + 1) {
        if (bit + std::size_t(size) > data.size() * 8) return false;
        int code = 0;
        for (int k = 0; k < size; ++k, ++bit) code |= ((static_cast<unsigned char>(data[bit / 8]) >> (bit % 8)) & 1) << k;
        if (code == clear) { reset(); size = minSize + 1; prev = -1; continue; }
        if (code == eoi) break;
        std::vector<std::uint8_t> entry;
        if (code < int(dict.size()) && (code < clear || !dict[std::size_t(code)].empty())) entry = dict[std::size_t(code)];
        else if (code == int(dict.size()) && prev >= 0) { entry = dict[std::size_t(prev)]; entry.push_back(dict[std::size_t(prev)][0]); }
        else return false;
        if (prev >= 0 && dict.size() < 4096) { std::vector<std::uint8_t> e = dict[std::size_t(prev)]; e.push_back(entry[0]); dict.push_back(std::move(e)); }
        out.insert(out.end(), entry.begin(), entry.end());
        if (int(dict.size()) == (1 << size) && size < 12) ++size;
        prev = code;
    }
    return out.size() == want;
}
}  // namespace

bool decodeGif(const std::string& bytes, GifImage& g, std::string& err) {
    Reader r{bytes};
    if (bytes.compare(0, 6, "GIF89a") != 0) { err = "not GIF89a"; return false; }
    r.p = 6;
    g.w = r.u16(); g.h = r.u16();
    const int packed = r.u8(); r.u8(); r.u8();
    if (!(packed & 0x80)) { err = "no global colour table"; return false; }
    for (int i = 0; i < (2 << (packed & 7)); ++i) { Rgb8 c{}; for (auto& v : c) v = std::uint8_t(r.u8()); g.palette.push_back(c); }
    std::vector<std::uint8_t> canvas(std::size_t(g.w) * g.h, 0);
    int delay = 0, trans = -1;
    while (!r.bad) {
        const int tag = r.u8();
        if (tag == ';') return true;
        if (tag == '!') {
            const int label = r.u8();
            const std::string body = r.blocks();
            if (label == 0xF9 && body.size() >= 4) { delay = int(static_cast<unsigned char>(body[1])) | (int(static_cast<unsigned char>(body[2])) << 8); trans = (body[0] & 1) ? int(static_cast<unsigned char>(body[3])) : -1; }
            if (label == 0xFF && body.size() >= 14 && body.compare(0, 11, "NETSCAPE2.0") == 0) g.loop = int(static_cast<unsigned char>(body[12])) | (int(static_cast<unsigned char>(body[13])) << 8);
            continue;
        }
        if (tag != ',') { err = "unknown block"; return false; }
        const int x = r.u16(), y = r.u16(), w = r.u16(), h = r.u16(), fp = r.u8();
        if (fp & 0xC0) { err = "local table or interlace not supported"; return false; }
        if (x + w > g.w || y + h > g.h) { err = "frame outside canvas"; return false; }
        const int minSize = r.u8();
        std::vector<std::uint8_t> px;
        if (!unlzw(r.blocks(), minSize, std::size_t(w) * h, px)) { err = "bad LZW data"; return false; }
        for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) {
            const std::uint8_t v = px[std::size_t(j) * w + i];
            if (int(v) != trans) canvas[std::size_t(y + j) * g.w + std::size_t(x + i)] = v;
        }
        g.frames.push_back(canvas);
        g.delaysCs.push_back(delay);
        trans = -1;
    }
    err = "truncated";
    return false;
}

}  // namespace raw::media
