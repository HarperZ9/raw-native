// raw_native_vk_probe [--checks] [--out FILE]: the hardware probe through Vulkan (HW H1.0;
// raw/rhi/hw_vk.hpp). raw_native_vk_probe --gemm [--sizes 1024,2048] [--warmup N] [--repeats N]
// [--out FILE]: H1.7, cooperative matrix GEMM. JSON on stdout (and in FILE). Exit 0 pass or
// probe only, 1 a bound missed, 2 bad arguments, 4 no usable device.
#include "raw/rhi/hw_vk.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>
int main(int argc, char** argv){
    bool checks = false, gemm = false;
    std::vector<uint32_t> sizes{1024, 2048, 4096};
    int warmup = 10, repeats = 100;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--checks") == 0) checks = true;
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (std::strcmp(argv[i], "--gemm") == 0) gemm = true;
        else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--sizes") == 0 && i + 1 < argc){
            sizes.clear();
            for (const char* p = argv[++i]; *p; ){ sizes.push_back((uint32_t)std::strtoul(p, const_cast<char**>(&p), 10)); if (*p == ',') ++p; else if (*p) break; }
        }
        else {
            std::printf("usage: raw_native_vk_probe [--checks] [--out FILE]\n"
                        "       raw_native_vk_probe --gemm [--sizes A,B] [--warmup N] [--repeats N] [--out FILE]\n");
            return 2;
        }
    }
    for (uint32_t n : sizes) if (n == 0 || n % 32){ std::printf("sizes must be positive multiples of 32\n"); return 2; }
    const raw::rhi::hw::VkRun r = gemm ? raw::rhi::hw::vulkanGemm(sizes, warmup, repeats) : raw::rhi::hw::vulkanProbe(checks);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.code;
}
