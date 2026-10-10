// raw_native_cli hw-rayquery (raw/tools/hw_rayquery_cmd.hpp; bounds in evidence/hw-h1-1-bounds.json).
#include "raw/tools/hw_rayquery_cmd.hpp"
#include "hw_rayquery_compare.hpp"
#include "raw/core/parallel.hpp"
#include "raw/tools/hw_scenes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
namespace raw {
namespace {
namespace hw = rhi::hw;
std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
double pct(std::vector<double> v, double p){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double k = p * (double)(v.size() - 1);
    const size_t i = (size_t)k;
    return i + 1 < v.size() ? v[i] + (k - (double)i) * (v[i + 1] - v[i]) : v[i];
}
std::string statsJson(const std::vector<double>& ms, size_t rays){
    if (ms.empty()) return "null";
    const double med = pct(ms, 0.5);
    return "{\"n\":" + std::to_string(ms.size()) + ",\"median_ms\":" + num(med) + ",\"p5_ms\":" + num(pct(ms, 0.05)) +
           ",\"p95_ms\":" + num(pct(ms, 0.95)) + ",\"p99_ms\":" + num(pct(ms, 0.99)) + ",\"iqr_ms\":" + num(pct(ms, 0.75) - pct(ms, 0.25)) +
           ",\"mrays_per_s_at_median\":" + num(med > 0 ? (double)rays / (med * 1e3) : 0) + "}";
}
std::string compareJson(const RayCompare& c){
    std::string s = "{\"rays\":" + std::to_string(c.rays) + ",\"both_miss\":" + std::to_string(c.bothMiss) + ",\"same_triangle\":" +
        std::to_string(c.sameTri) + ",\"t_violations\":" + std::to_string(c.violT) + ",\"uv_violations\":" + std::to_string(c.violUV) +
        ",\"worst_t_over_tol\":" + num(c.worstT) + ",\"worst_uv_over_tol\":" + num(c.worstUV) + ",\"explained\":{\"edge\":" +
        std::to_string(c.edge) + ",\"tie\":" + std::to_string(c.tie) + ",\"grazing\":" + std::to_string(c.grazing) + ",\"boundary\":" +
        std::to_string(c.boundary) + "},\"unexplained\":" + std::to_string(c.unexplained) + ",\"unexplained_samples\":[";
    for (size_t i = 0; i < c.samples.size(); ++i) s += (i ? "," : "") + std::string("\"") + hw::jsonEscape(c.samples[i]) + "\"";
    return s + "]}";
}
bool within(const RayCompare& c){ return c.unexplained == 0 && c.violT == 0 && c.violUV == 0 && (double)c.explained() <= 1e-3 * (double)c.rays; }
bool controlFails(const RayCompare& c){ return c.unexplained + c.violT + c.violUV > 0; }
// Rotate every direction by one degree about normalize(1, 2, 3) (Rodrigues).
std::vector<hw::RayIn> rotated(std::vector<hw::RayIn> rays){
    const Vec3 k = normalize(Vec3{1, 2, 3});
    const float a = 3.14159265358979f / 180, c = std::cos(a), s = std::sin(a);
    for (auto& r : rays){
        const Vec3 v{r.dx, r.dy, r.dz};
        const Vec3 o = v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
        r.dx = o.x; r.dy = o.y; r.dz = o.z;
    }
    return rays;
}
// The CPU reference: closest hits from the M3 BVH, in parallel.
void cpuReference(const HwScene& s, const Bvh& bvh, std::vector<Hit>& cpu, std::vector<bool>& cpuHit){
    const int threads = std::max(1, (int)std::thread::hardware_concurrency());
    cpu.assign(s.rays.size(), {});
    std::vector<char> hitc(s.rays.size());
    constexpr int chunks = 256;
    parallelRows(chunks, threads, [&](int ch){
        for (size_t i = (size_t)ch; i < s.rays.size(); i += chunks){
            const auto& r = s.rays[i];
            hitc[i] = bvh.closest(Ray{{r.ox, r.oy, r.oz}, {r.dx, r.dy, r.dz}}, r.tmax, cpu[i]) ? 1 : 0;
        }
    });
    cpuHit.assign(hitc.begin(), hitc.end());
}
// GPU hits by ray kind, each kind timed on its own; kinds receives the timing JSON.
bool gpuByKind(rhi::Device& dev, const HwScene& s, const std::vector<float>& flat, int warmup, int repeats,
               std::vector<hw::HitOut>& gpu, std::string& kinds, std::string& err){
    gpu.assign(s.rays.size(), {});
    for (size_t ki = 0; ki < s.kinds.size(); ++ki){
        const HwRayKind& k = s.kinds[ki];
        std::vector<hw::RayIn> part(s.rays.begin() + (long)k.begin, s.rays.begin() + (long)k.end);
        std::vector<hw::HitOut> out;
        hw::TraceOptions opt; opt.warmup = warmup; opt.repeats = repeats;
        const hw::TraceResult t = hw::traceRayQuery(dev, flat, part, opt, out);
        if (!t.ran){ err = t.error; return false; }
        std::copy(out.begin(), out.end(), gpu.begin() + (long)k.begin);
        kinds += (ki ? "," : "") + std::string("{\"kind\":\"") + k.kind + "\",\"rays\":" + std::to_string(part.size()) +
                 ",\"build_ms\":" + num(t.buildMs) + ",\"trace\":" + statsJson(t.traceMs, part.size()) + "}";
    }
    return true;
}
// One scene: the comparison and both controls. Returns the scene's JSON body; ok and fallback report the verdict.
std::string sceneRun(rhi::Device& dev, const HwScene& s, int warmup, int repeats, bool& ok, bool& fallback){
    Bvh bvh;
    bvh.build(s.tris);
    std::vector<Hit> cpu;
    std::vector<bool> cpuHit;
    cpuReference(s, bvh, cpu, cpuHit);
    const std::vector<float> flat = flatten(s.tris);
    std::vector<hw::HitOut> gpu;
    std::string kinds, err;
    if (!gpuByKind(dev, s, flat, warmup, repeats, gpu, kinds, err)){
        // The fallback is the CPU traversal itself; it agrees with the reference by construction.
        fallback = true;
        ok = hw::disabled(hw::kRayQuery);
        return "\"fallback\":\"cpu traversal\",\"reason\":\"" + hw::jsonEscape(err) + "\"";
    }
    const RayCompare main = compareHits(s.tris, s.rays, cpu, cpuHit, gpu);
    std::vector<hw::HitOut> off, rot;
    hw::TraceOptions o1; o1.offset[0] = o1.offset[1] = o1.offset[2] = 1e-3f;
    const hw::TraceResult t1 = hw::traceRayQuery(dev, flat, s.rays, o1, off);
    const hw::TraceResult t2 = hw::traceRayQuery(dev, flat, rotated(s.rays), {}, rot);
    const RayCompare c1 = t1.ran ? compareHits(s.tris, s.rays, cpu, cpuHit, off) : RayCompare{};
    const RayCompare c2 = t2.ran ? compareHits(s.tris, s.rays, cpu, cpuHit, rot) : RayCompare{};
    const bool w = within(main), f1 = t1.ran && controlFails(c1), f2 = t2.ran && controlFails(c2);
    ok = w && f1 && f2;
    return "\"triangles\":" + std::to_string(s.tris.size()) + ",\"bvh_nodes\":" + std::to_string(bvh.nodeCount()) +
           ",\"kinds\":[" + kinds + "],\"compare\":" + compareJson(main) + ",\"control_offset\":" + compareJson(c1) +
           ",\"control_rotate\":" + compareJson(c2) + ",\"within_bounds\":" + (w ? "true" : "false") +
           ",\"control_offset_fails\":" + (f1 ? "true" : "false") + ",\"control_rotate_fails\":" + (f2 ? "true" : "false");
}
}  // namespace

HwRayQueryReport hwRayQuery(rhi::Device& dev, int warmup, int repeats){
    HwRayQueryReport rep;
    const std::vector<HwScene> scenes = hwScenes();
    bool pass = true, fallback = false;
    std::string sj;
    for (size_t si = 0; si < scenes.size(); ++si){
        bool ok = false;
        const std::string body = sceneRun(dev, scenes[si], warmup, repeats, ok, fallback);
        pass = pass && ok;
        sj += (si ? ",\n  " : "") + std::string("{\"scene\":\"") + scenes[si].name + "\"," + body + "}";
    }
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    rep.pass = pass;
    rep.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-1-bounds.json\",\n \"backend\": \"" +
               hw::jsonEscape(dev.backendName()) + "\",\n \"adapter\": \"" + hw::jsonEscape(dev.adapter().description) +
               "\",\n \"driver\": \"" + hw::jsonEscape(dev.adapter().driver) + "\",\n \"disable\": \"" + hw::jsonEscape(env ? env : "") +
               "\",\n \"fallback\": " + (fallback ? "true" : "false") + ",\n \"warmup\": " + std::to_string(warmup) +
               ",\n \"repeats\": " + std::to_string(repeats) + ",\n \"scenes\": [\n  " + sj + "\n ],\n \"pass\": " + (pass ? "true" : "false") + "\n}\n";
    return rep;
}

int hwRayQueryCommand(int argc, char** argv){
    int warmup = 2, repeats = 5;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli hw-rayquery [--warmup N] [--repeats N] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev){ std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    const HwRayQueryReport r = hwRayQuery(*dev, warmup, repeats);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
}  // namespace raw
