#include "raw/core/image.hpp"
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sstream>
namespace raw {
static unsigned char to8(float v){
    v = std::clamp(v, 0.0f, 1.0f); return (unsigned char)(v*255.0f + 0.5f); }
void writePPM(const Buffer<Vec3>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        const Vec3& c = img.at(x,y);
        unsigned char rgb[3] = {to8(c.x), to8(c.y), to8(c.z)};
        f.write((char*)rgb, 3); }
}
std::string rgb8Bytes(const Buffer<Vec3>& img){
    std::string b; b.reserve((size_t)img.w * img.h * 3);
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        const Vec3& c = img.at(x,y);
        b += (char)to8(c.x); b += (char)to8(c.y); b += (char)to8(c.z); }
    return b;
}
std::string f32Bytes(const Buffer<float>& img){
    std::string b((size_t)img.w * img.h * 4, '\0');
    for (size_t i = 0; i < img.px.size(); ++i){
        uint32_t u; std::memcpy(&u, &img.px[i], 4);   // little-endian whatever the host order
        for (int k = 0; k < 4; ++k) b[4*i + k] = (char)((u >> (8*k)) & 0xFF);
    }
    return b;
}
void writePGM(const Buffer<float>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        unsigned char v = to8(img.at(x,y)); f.write((char*)&v, 1); }
}
void writePFM(const Buffer<Vec3>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    // PFM header: "PF" (RGB), width height, then a negative scale = little-endian.
    f << "PF\n" << img.w << " " << img.h << "\n-1.0\n";
    // PFM stores rows bottom-to-top; write exact float radiance, no clamp.
    for (int y = img.h - 1; y >= 0; --y) for (int x = 0; x < img.w; ++x){
        const Vec3& c = img.at(x,y);
        float rgb[3] = { c.x, c.y, c.z };
        char bytes[12];
        std::memcpy(bytes, rgb, 12);
        f.write(bytes, 12);
    }
}
void writePFM1(const Buffer<float>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "Pf\n" << img.w << " " << img.h << "\n-1.0\n";
    for (int y = img.h - 1; y >= 0; --y) for (int x = 0; x < img.w; ++x){
        float v = img.at(x,y);
        char bytes[4];
        std::memcpy(bytes, &v, 4);
        f.write(bytes, 4);
    }
}
void writeMaskPGM(const Buffer<uint8_t>& mask, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << mask.w << " " << mask.h << "\n255\n";
    for (int y=0;y<mask.h;++y) for (int x=0;x<mask.w;++x){
        unsigned char v = mask.at(x,y) ? 255 : 0; f.write((char*)&v, 1); }
}
// Read a whole file and split off a three-line header ("magic\nW H\nlast\n").
static bool readHeader(const std::string& path, const char* magic, std::string& last,
                       int& w, int& h, std::string& body){
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string all((std::istreambuf_iterator<char>(f)), {});
    size_t a = all.find('\n'); if (a == std::string::npos) return false;
    size_t b = all.find('\n', a + 1); if (b == std::string::npos) return false;
    size_t c = all.find('\n', b + 1); if (c == std::string::npos) return false;
    if (all.substr(0, a) != magic) return false;
    std::istringstream dims(all.substr(a + 1, b - a - 1));
    if (!(dims >> w >> h) || w <= 0 || h <= 0) return false;
    last = all.substr(b + 1, c - b - 1);
    body = all.substr(c + 1);
    return true;
}
bool readPFM1(const std::string& path, Buffer<float>& out){
    std::string scale, body; int w = 0, h = 0;
    if (!readHeader(path, "Pf", scale, w, h, body) || scale != "-1.0") return false;
    if (body.size() != (size_t)w * h * 4) return false;
    out.resize(w, h);
    size_t k = 0;
    for (int y = h - 1; y >= 0; --y) for (int x = 0; x < w; ++x, k += 4)
        std::memcpy(&out.at(x,y), body.data() + k, 4);
    return true;
}
bool readMaskPGM(const std::string& path, Buffer<uint8_t>& out){
    std::string maxv, body; int w = 0, h = 0;
    if (!readHeader(path, "P5", maxv, w, h, body) || maxv != "255") return false;
    if (body.size() != (size_t)w * h) return false;
    out.resize(w, h);
    for (size_t i = 0; i < body.size(); ++i) out.px[i] = body[i] ? 1 : 0;
    return true;
}
}
