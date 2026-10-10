// Fuzz the glTF importer (ROADMAP M2 criterion 2): mutate valid seed files and require
// that every input loads or throws AssetError, with no crash, no other exception, and no
// input slower than the limit. Built with the sanitizers in CI, so a memory error or
// undefined behaviour is a reported failure, not a silent one.
//   fuzz_gltf [count=1000000] [seed=1] [limit_ms=1000] [out.json]
#include "raw/assets/gltf.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
using namespace raw::assets;

template <class T> static void put(std::vector<std::uint8_t>& b, T v){ const auto* p = reinterpret_cast<const std::uint8_t*>(&v); b.insert(b.end(), p, p + sizeof v); }
static std::string b64(const std::vector<std::uint8_t>& d){
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (std::size_t i = 0; i < d.size(); i += 3){
        const std::uint32_t v = std::uint32_t(d[i]) << 16 | (i + 1 < d.size() ? d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
        o += T[v >> 18 & 63]; o += T[v >> 12 & 63]; o += i + 1 < d.size() ? T[v >> 6 & 63] : '='; o += i + 2 < d.size() ? T[v & 63] : '=';
    }
    return o;
}

// Seeds: a box (positions, normals, uint32 indices, nodes with TRS, a matrix and a child)
// as .gltf with a data uri and as .glb, and a minimal triangle with uint8 indices.
static std::vector<std::vector<std::uint8_t>> seeds(){
    std::vector<std::uint8_t> box;
    const float P[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    for (auto& p : P) for (float f : p) put(box, f);
    for (auto& p : P) for (float f : p) put(box, f * 0.57735f);
    const std::uint32_t I[36] = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,2,3,7,2,7,6,1,2,6,1,6,5,0,4,7,0,7,3};
    for (auto i : I) put(box, i);
    const std::string json = std::string(R"({"asset":{"version":"2.0","generator":"raw-native fuzz seed"},"scene":0,"scenes":[{"nodes":[0,1]}],)") +
        R"("nodes":[{"mesh":0,"rotation":[0,0.3826834,0,0.9238795],"translation":[0,1,0]},{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,3,0,0,1],"children":[2]},{"mesh":0,"scale":[0.5,0.5,0.5]}],)" +
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0,"mode":4}]}],)" +
        R"("materials":[{"name":"clay","pbrMetallicRoughness":{"baseColorFactor":[0.8,0.4,0.3,1],"metallicFactor":0}}],)" +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-1,-1,-1],"max":[1,1,1]},{"bufferView":0,"byteOffset":96,"componentType":5126,"count":8,"type":"VEC3"},{"bufferView":1,"componentType":5125,"count":36,"type":"SCALAR"}],)" +
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":192,"byteStride":12},{"buffer":0,"byteOffset":192,"byteLength":144}],)";
    std::vector<std::vector<std::uint8_t>> out;
    const std::string gltf = json + R"("buffers":[{"byteLength":336,"uri":"data:application/gltf-buffer;base64,)" + b64(box) + "\"}]}";
    out.emplace_back(gltf.begin(), gltf.end());
    std::string j2 = json + R"("buffers":[{"byteLength":336}]})";
    while (j2.size() % 4) j2 += ' ';
    std::vector<std::uint8_t> glb;
    put(glb, std::uint32_t(0x46546C67)); put(glb, std::uint32_t(2)); put(glb, std::uint32_t(12 + 8 + j2.size() + 8 + box.size()));
    put(glb, std::uint32_t(j2.size())); put(glb, std::uint32_t(0x4E4F534A)); glb.insert(glb.end(), j2.begin(), j2.end());
    put(glb, std::uint32_t(box.size())); put(glb, std::uint32_t(0x004E4942)); glb.insert(glb.end(), box.begin(), box.end());
    out.push_back(glb);
    std::vector<std::uint8_t> tri;
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) put(tri, f);
    for (std::uint8_t i : {0, 1, 2}) tri.push_back(i);
    const std::string t = std::string(R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)") +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"byteOffset":36,"componentType":5121,"count":3,"type":"SCALAR"}],)" +
        R"("bufferViews":[{"buffer":0,"byteLength":39}],"buffers":[{"byteLength":39,"uri":"data:application/octet-stream;base64,)" + b64(tri) + "\"}]}";
    out.emplace_back(t.begin(), t.end());
    return out;
}

int main(int argc, char** argv){
    const long long N = argc > 1 ? std::atoll(argv[1]) : 1000000;
    const unsigned seed = argc > 2 ? unsigned(std::atoll(argv[2])) : 1u;
    const double limit = argc > 3 ? std::atof(argv[3]) : 1000.0;
    const auto S = seeds();
    for (const auto& s : S) (void)loadGltf(s);               // every seed loads
    std::mt19937_64 r(seed);
    auto U = [&](std::size_t n){ return n ? std::size_t(r() % n) : std::size_t(0); };
    static const char* tokens[] = {"{", "}", "[", "]", ",", ":", "\"", "0", "-1", "1e308", "4294967295", "2147483648", "65536", "null", "true", "\\u0000", "5126", "5125", "VEC3", "MAT4"};
    long long loaded = 0, refused = 0, other = 0, slow = 0;
    double maxMs = 0;
    std::string firstOther;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long n = 0; n < N; ++n){
        std::vector<std::uint8_t> b = S[U(S.size())];
        for (std::size_t k = 1 + U(8); k > 0; --k){
            const std::size_t i = U(b.size() + 1);
            switch (U(7)){
            case 0: if (!b.empty()) b[std::min(i, b.size() - 1)] ^= std::uint8_t(1u << U(8)); break;
            case 1: b.insert(b.begin() + std::ptrdiff_t(i), std::uint8_t(r())); break;
            case 2: { const char* t = tokens[U(sizeof tokens / sizeof *tokens)]; b.insert(b.begin() + std::ptrdiff_t(i), t, t + std::strlen(t)); break; }
            case 3: b.erase(b.begin() + std::ptrdiff_t(i), b.begin() + std::ptrdiff_t(std::min(b.size(), i + 1 + U(16)))); break;
            case 4: { const std::size_t at = std::min(i, b.size()), len = std::min(b.size() - at, 1 + U(32));
                      const std::vector<std::uint8_t> run(b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + len)); b.insert(b.begin() + std::ptrdiff_t(at), run.begin(), run.end()); break; }
            case 5: b.resize(i); break;
            default: if (b.size() >= 4){ const std::size_t j = U(b.size() - 3); const std::uint32_t v = std::uint32_t(r()); std::memcpy(&b[j], &v, 4); } break;
            }
        }
        if (std::getenv("FUZZ_DUMP")) { std::ofstream f("fuzz_last.bin", std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())); }
        const auto a = std::chrono::steady_clock::now();
        try { (void)loadGltf(b); ++loaded; }
        catch (const AssetError&){ ++refused; }
        catch (const std::exception& e){ if (!other++) firstOther = e.what(); }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        if (ms > maxMs) maxMs = ms;
        if (ms > limit) ++slow;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char buf[1024];
    std::snprintf(buf, sizeof buf, "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"M2 exit criterion 2 (one million fuzzed glTF inputs: no crash, hang or sanitizer report)\",\n"
        " \"inputs\": %lld,\n \"seed\": %u,\n \"loaded\": %lld,\n \"refused\": %lld,\n \"other_exceptions\": %lld,\n \"slow\": %lld,\n \"limit_ms\": %.0f,\n \"max_ms\": %.3f,\n \"seconds\": %.1f,\n \"pass\": %s\n}\n",
        N, seed, loaded, refused, other, slow, limit, maxMs, secs, other || slow ? "false" : "true");
    std::fputs(buf, stdout);
    if (other) std::fprintf(stderr, "first other exception: %s\n", firstOther.c_str());
    if (argc > 4) std::ofstream(argv[4]) << buf;
    return other || slow ? 1 : 0;
}
