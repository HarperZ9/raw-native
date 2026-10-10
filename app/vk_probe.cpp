// raw_native_vk_probe [--checks] [--out FILE]: the hardware probe through Vulkan (HW H1.0;
// raw/rhi/hw_vk.hpp). JSON on stdout (and in FILE). Exit 0 pass or probe only, 1 a bound
// missed, 2 bad arguments, 4 no usable device.
#include "raw/rhi/hw_vk.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
int main(int argc, char** argv){
    bool checks = false;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--checks") == 0) checks = true;
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_vk_probe [--checks] [--out FILE]\n"); return 2; }
    }
    const raw::rhi::hw::VkRun r = raw::rhi::hw::vulkanProbe(checks);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.code;
}
