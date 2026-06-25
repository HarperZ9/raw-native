#include "raw/image.hpp"
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <cstring>
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
}
