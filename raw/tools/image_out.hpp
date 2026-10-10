#pragma once
// Showcase image output for the CLI's render commands: exposed linear radiance written as a
// float PFM and, through a display pipeline (raw/renderer/colour.hpp), as an 8-bit PNG with
// stored deflate blocks (no compression library: any viewer reads it).
#include "raw/renderer/lighting.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw {
bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgb);
// stem.pfm (linear) and stem.png (pipeline, default AgX to sRGB). False when unwritable.
bool writeImages(const std::string& stem, int w, int h, const std::vector<lighting::Rgb>& img, const char* pipeline = "agx/srgb");
}
