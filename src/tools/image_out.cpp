// Showcase image output: see raw/tools/image_out.hpp.
#include "raw/tools/image_out.hpp"
#include "raw/renderer/colour.hpp"
#include "raw/core/image.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
namespace raw {
// A PNG with stored (uncompressed) deflate blocks: no dependency, any viewer reads it.
namespace {
std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    for (std::size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}
void be32(std::string& s, std::uint32_t v) { for (int k = 3; k >= 0; --k) s.push_back(char((v >> (8 * k)) & 0xFF)); }
void chunk(std::string& out, const char* type, const std::string& data) {
    be32(out, std::uint32_t(data.size()));
    std::string td = std::string(type, 4) + data;
    out += td;
    be32(out, crc32(reinterpret_cast<const std::uint8_t*>(td.data()), td.size()) ^ 0xFFFFFFFFu);
}
}  // namespace

bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgb) {
    std::string raw, z = "\x78\x01", out = "\x89PNG\r\n\x1a\n", ihdr;
    for (int y = 0; y < h; ++y) { raw.push_back('\0'); raw.append(reinterpret_cast<const char*>(rgb.data()) + std::size_t(y) * w * 3, std::size_t(w) * 3); }
    std::uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) { a = (a + ch) % 65521u; b = (b + a) % 65521u; }
    for (std::size_t i = 0; i < raw.size(); i += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - i);
        z.push_back(char(i + n >= raw.size() ? 1 : 0));
        z.push_back(char(n & 0xFF)); z.push_back(char(n >> 8)); z.push_back(char(~n & 0xFF)); z.push_back(char((~n >> 8) & 0xFF));
        z.append(raw, i, n);
    }
    be32(z, (b << 16) | a);
    be32(ihdr, std::uint32_t(w)); be32(ihdr, std::uint32_t(h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);
    chunk(out, "IHDR", ihdr); chunk(out, "IDAT", z); chunk(out, "IEND", "");
    std::ofstream f(path, std::ios::binary);
    f << out;
    return bool(f);
}
// Exposed linear radiance to display: AgX into sRGB, then 8 bits; and the linear PFM.
bool writeImages(const std::string& stem, int w, int h, const std::vector<lighting::Rgb>& img, const char* pipeline) {
    colour::Pipeline pl;
    if (!colour::parsePipeline(pipeline, pl)) return false;
    const colour::Transform tf(pl);
    std::vector<std::uint8_t> rgb(std::size_t(w) * h * 3);
    Buffer<Vec3> pfm; pfm.resize(w, h);
    for (std::size_t i = 0; i < img.size(); ++i) {
        const colour::RGB d = tf.apply({float(img[i].r), float(img[i].g), float(img[i].b)});
        for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = std::uint8_t(std::lround(std::clamp(d[std::size_t(k)], 0.0f, 1.0f) * 255.0f));
        pfm.px[i] = {float(img[i].r), float(img[i].g), float(img[i].b)};
    }
    writePFM(pfm, stem + ".pfm");
    return writePng(stem + ".png", w, h, rgb);
}
}  // namespace raw
