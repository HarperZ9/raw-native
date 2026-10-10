// raw_native_cli hw-mesh: HW H1.3, mesh and amplification shaders against vertex pulling
// (raw/tools/hw_mesh_cmd.hpp; bounds in evidence/hw-h1-3-bounds.json).
#include "raw/tools/hw_mesh_cmd.hpp"
#include "hw_stats.hpp"
#include "raw/rhi/hw.hpp"
#include "raw/tools/hw_scenes.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace raw {
namespace {
namespace hw = rhi::hw;
using hwstats::num;
constexpr float kPi = 3.14159265358979f;
struct Camera { const char* name; Vec3 eye, at; };
// The three cameras per scene, fixed before the first run (bounds: "cameras").
const Camera kTerrain[3] = {{"wide", {-9, 5, -9}, {0, 0, 0}}, {"close", {-2, 1.5f, -2}, {0, 0.5f, 1}}, {"grazing", {-8, 0.6f, -8}, {8, 0, 8}}};
const Camera kHall[3] = {{"wide", {-9, 3, -5}, {5, 1.5f, 3}}, {"close", {-3, 1.8f, -3.5f}, {-4.5f, 1.5f, -1.5f}}, {"grazing", {-9.5f, 0.3f, 0}, {9, 0.2f, 0}}};

uint32_t bits(float f){ uint32_t u; std::memcpy(&u, &f, 4); return u; }
// Meshlets of up to 32 consecutive triangles: bounding sphere and normal cone.
std::vector<uint32_t> meshlets(const std::vector<Tri>& tris){
    std::vector<uint32_t> out;
    for (size_t first = 0; first < tris.size(); first += 32){
        const size_t n = std::min<size_t>(32, tris.size() - first);
        AABB box;
        Vec3 sum{0, 0, 0};
        std::vector<Vec3> normals;
        bool degenerate = false;
        for (size_t i = first; i < first + n; ++i){
            const Tri& t = tris[i];
            box.grow(t.a); box.grow(t.b); box.grow(t.c);
            const Vec3 c = cross(t.b - t.a, t.c - t.a);
            if (length(c) == 0){ degenerate = true; continue; }
            normals.push_back(normalize(c)); sum = sum + normals.back();
        }
        const Vec3 centre = (box.mn + box.mx) * 0.5f;
        float radius = 0;
        for (size_t i = first; i < first + n; ++i)
            for (Vec3 p : {tris[i].a, tris[i].b, tris[i].c}) radius = std::fmax(radius, length(p - centre));
        Vec3 axis{0, 0, 1};
        float theta = -1;
        if (!degenerate && length(sum) > 1e-3f){
            axis = normalize(sum);
            theta = 0;
            for (Vec3 nn : normals) theta = std::fmax(theta, std::acos(std::fmin(1.0f, std::fmax(-1.0f, dot(nn, axis)))));
            if (theta >= 0.5f * kPi) theta = -1;
        }
        const uint32_t m[12] = {(uint32_t)first, (uint32_t)n, 0, 0, bits(centre.x), bits(centre.y), bits(centre.z), bits(radius),
                                bits(axis.x), bits(axis.y), bits(axis.z), bits(theta)};
        out.insert(out.end(), m, m + 12);
    }
    return out;
}
// Right-handed view, D3D projection (depth 0 to 1), as rows; frustum planes from the rows.
void constants(const Camera& c, uint32_t w, uint32_t h, uint32_t count, uint32_t out[48]){
    const Vec3 f = normalize(c.at - c.eye), r = normalize(cross(f, {0, 1, 0})), u = cross(r, f);
    const double V[4][4] = {{r.x, r.y, r.z, -dot(r, c.eye)}, {u.x, u.y, u.z, -dot(u, c.eye)}, {-f.x, -f.y, -f.z, dot(f, c.eye)}, {0, 0, 0, 1}};
    const double t = std::tan(kPi / 6), a = (double)w / h, n = 0.05, F = 100;
    const double P[4][4] = {{1 / (a * t), 0, 0, 0}, {0, 1 / t, 0, 0}, {0, 0, F / (n - F), n * F / (n - F)}, {0, 0, -1, 0}};
    float M[4][4];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j){ double s = 0; for (int k = 0; k < 4; ++k) s += P[i][k] * V[k][j]; M[i][j] = (float)s; }
    float pl[6][4];
    for (int j = 0; j < 4; ++j){
        pl[0][j] = M[3][j] + M[0][j]; pl[1][j] = M[3][j] - M[0][j]; pl[2][j] = M[3][j] + M[1][j];
        pl[3][j] = M[3][j] - M[1][j]; pl[4][j] = M[2][j]; pl[5][j] = M[3][j] - M[2][j];
    }
    for (auto& p : pl){ const float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); for (float& x : p) x /= l; }
    std::memset(out, 0, 48 * 4);
    for (int i = 0; i < 16; ++i) out[i] = bits(M[i / 4][i % 4]);
    for (int i = 0; i < 24; ++i) out[16 + i] = bits(pl[i / 4][i % 4]);
    out[40] = bits(c.eye.x); out[41] = bits(c.eye.y); out[42] = bits(c.eye.z); out[44] = count;
}
uint64_t diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b){
    if (a.size() != b.size()) return ~0ull;
    uint64_t d = 0; for (size_t i = 0; i < a.size(); ++i) d += a[i] != b[i]; return d;
}
uint64_t diffF(const std::vector<float>& a, const std::vector<float>& b){
    if (a.size() != b.size()) return ~0ull;
    uint64_t d = 0; for (size_t i = 0; i < a.size(); ++i) d += std::memcmp(&a[i], &b[i], 4) != 0; return d;
}
// One scene and camera: both paths, the control, timings. ok and exercised report the verdict.
std::string viewRun(rhi::Device& dev, hw::VisInput& in, const Camera& cam, uint32_t total, int warmup, int repeats, bool& ok, bool& exercised){
    constants(cam, in.width, in.height, total, in.constants);
    const hw::VisDraw v = hw::drawVisibility(dev, in, hw::GeomPath::Vertex, warmup, repeats);
    const hw::VisDraw m = hw::drawVisibility(dev, in, hw::GeomPath::Mesh, warmup, repeats);
    const hw::VisDraw c = hw::drawVisibility(dev, in, hw::GeomPath::MeshConeFlipControl, 0, 0);
    const bool forced = hw::disabled(hw::kMeshShader);
    uint64_t covered = 0;
    for (uint32_t id : v.ids) covered += id != 0xFFFFFFFFu;
    const uint64_t di = m.ran ? diff(v.ids, m.ids) : 0, dd = m.ran ? diffF(v.depth, m.depth) : 0, dc = c.ran ? diff(v.ids, c.ids) : 0;
    ok = v.ran && (forced ? !m.ran : (m.ran && di == 0 && dd == 0 && c.ran && dc > 0));
    exercised = m.ran && m.keptMeshlets < total;
    std::string gain = "null";
    if (!v.ms.empty() && !m.ms.empty()){
        double r = 0, lo = 0, hi = 0;
        hwstats::ratioInterval(v.ms, m.ms, r, lo, hi);
        gain = "{\"median_ratio\":" + num(r) + ",\"ci95\":[" + num(lo) + "," + num(hi) + "]}";
    }
    return std::string("{\"camera\":\"") + cam.name + "\",\"covered_pixels\":" + std::to_string(covered) +
           ",\"id_differences\":" + (m.ran ? std::to_string(di) : "null") + ",\"depth_differences\":" + (m.ran ? std::to_string(dd) : "null") +
           ",\"meshlets\":" + std::to_string(total) + ",\"kept\":" + (m.ran ? std::to_string(m.keptMeshlets) : "null") +
           ",\"control_id_differences\":" + (c.ran ? std::to_string(dc) : "null") + ",\"error\":\"" + hw::jsonEscape(v.error + m.error) +
           "\",\"vertex_time\":" + hwstats::summary(v.ms) + ",\"mesh_time\":" + hwstats::summary(m.ms) + ",\"mesh_speedup\":" + gain +
           ",\"pass\":" + (ok ? "true" : "false") + "}";
}
}  // namespace

HwMeshReport hwMesh(rhi::Device& dev, int warmup, int repeats){
    HwMeshReport rep;
    const std::vector<HwScene> scenes = hwScenes();
    bool pass = true, closeExercised = true;
    std::string sj;
    for (int si = 0; si < 2; ++si){
        const HwScene& s = scenes[(size_t)si];   // terrain, hall
        hw::VisInput in;
        in.triangles = flatten(s.tris); in.meshlets = meshlets(s.tris); in.width = 1920; in.height = 1080;
        const uint32_t total = (uint32_t)(in.meshlets.size() / 12);
        std::string vj;
        for (int ci = 0; ci < 3; ++ci){
            const Camera& cam = si == 0 ? kTerrain[ci] : kHall[ci];
            bool ok = false, ex = false;
            vj += (ci ? "," : "") + viewRun(dev, in, cam, total, warmup, repeats, ok, ex);
            pass = pass && ok;
            if (std::strcmp(cam.name, "close") == 0) closeExercised = closeExercised && ex;
        }
        sj += (si ? ",\n  " : "") + std::string("{\"scene\":\"") + s.name + "\",\"views\":[" + vj + "]}";
    }
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    rep.pass = pass;
    rep.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-3-bounds.json\",\n \"adapter\": \"" +
               hw::jsonEscape(dev.adapter().description) + "\",\n \"disable\": \"" + hw::jsonEscape(env ? env : "") + "\",\n \"warmup\": " +
               std::to_string(warmup) + ",\n \"repeats\": " + std::to_string(repeats) + ",\n \"scenes\": [\n  " + sj +
               "\n ],\n \"culling_exercised_on_close_cameras\": " + (closeExercised ? "true" : "false") + ",\n \"pass\": " + (pass ? "true" : "false") + "\n}\n";
    return rep;
}

int hwMeshCommand(int argc, char** argv){
    int warmup = 1, repeats = 5;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli hw-mesh [--warmup N] [--repeats N] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev){ std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    const HwMeshReport r = hwMesh(*dev, warmup, repeats);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
}  // namespace raw
