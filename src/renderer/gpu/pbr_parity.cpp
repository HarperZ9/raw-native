// GPU parity of the material model: see raw/renderer/pbr_parity.hpp.
#include "raw/renderer/pbr_parity.hpp"
#include "shader_library.hpp"
#include "shaders/pbr_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace raw::gpu_check {
namespace {
using pbr::D3;
using pbr::Rgb;
struct Rng {
    std::uint64_t s;
    double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) * (1.0 / 9007199254740992.0); }
};
D3 dir(Rng& r, bool below) {
    const double mu = 0.03 + 0.97 * r.next(), ph = 2.0 * pbr::kPi * r.next(), s = std::sqrt(1.0 - mu * mu);
    return {s * std::cos(ph), s * std::sin(ph), below ? -mu : mu};
}
Rgb col(Rng& r, double lo = 0.0) { return {lo + (1 - lo) * r.next(), lo + (1 - lo) * r.next(), lo + (1 - lo) * r.next()}; }
constexpr int kFamilies = 10;
void family(pbr::Material& m, Rng& r, int f) {
    switch (f) {
    case 1: m.metallic = 0.0; break;
    case 2: m.clearcoat = r.next(); m.clearcoatRoughness = r.next(); m.coatNormal = {0.3 * (r.next() - 0.5), 0.3 * (r.next() - 0.5), 1.0}; break;
    case 3: m.sheenColor = col(r); m.sheenRoughness = r.next(); break;
    case 4: m.metallic *= 0.5; m.transmission = r.next(); break;
    case 5: m.metallic *= 0.5; m.transmission = r.next(); m.volume = true; m.thickness = 2.0 * r.next();
            m.attenuationDistance = 0.2 + 2.0 * r.next(); m.attenuationColor = col(r, 0.05); break;
    case 6: m.anisotropy = r.next(); m.anisotropyRotation = 2.0 * pbr::kPi * r.next(); break;
    case 7: m.iridescence = r.next(); m.iridescenceIor = 1.2 + 0.8 * r.next(); m.iridescenceThickness = 100.0 + 900.0 * r.next(); break;
    case 8: m.clearcoat = r.next(); m.sheenColor = col(r); m.anisotropy = r.next(); m.iridescence = r.next(); m.transmission = r.next(); break;
    case 9: m.emissive = col(r); m.emissiveStrength = 10.0 * r.next(); break;
    default: break;
    }
}
float f32(double v) { return float(v); }
void put3(float* o, Rgb& c) { o[0] = f32(c.r); o[1] = f32(c.g); o[2] = f32(c.b); c = {o[0], o[1], o[2]}; }
void put3(float* o, D3& c) { o[0] = f32(c.x); o[1] = f32(c.y); o[2] = f32(c.z); c = {o[0], o[1], o[2]}; }
void put(float* o, double& v) { *o = f32(v); v = *o; }
}  // namespace

std::vector<PbrCase> pbrCases(std::uint32_t count, std::uint64_t seed) {
    Rng r{seed * 0x9E3779B97F4A7C15ull + 1};
    std::vector<PbrCase> out;
    for (std::uint32_t k = 0; k < count; ++k) {
        PbrCase c;
        c.family = int(k % kFamilies);
        c.m.baseColor = col(r); c.m.metallic = r.next(); c.m.roughness = r.next();
        c.m.ior = 1.0 + 1.5 * r.next(); c.m.specular = r.next(); c.m.specularColor = col(r);
        family(c.m, r, c.family);
        const bool below = c.m.transmission > 0.0 && r.next() < 0.4;
        c.wo = dir(r, false); c.wi = dir(r, below);
        out.push_back(c);
    }
    return out;
}

void packCase(PbrCase& c, float* o) {
    pbr::Material& m = c.m;
    std::fill(o, o + kPbrCaseFloats, 0.0f);
    put3(o + 0, m.baseColor); put(o + 3, m.metallic); put(o + 4, m.roughness); put(o + 5, m.ior);
    put(o + 6, m.specular); put3(o + 7, m.specularColor); put(o + 10, m.clearcoat); put(o + 11, m.clearcoatRoughness);
    put3(o + 12, m.coatNormal); put3(o + 15, m.sheenColor); put(o + 18, m.sheenRoughness); put(o + 19, m.transmission);
    o[20] = m.volume ? 1.0f : 0.0f;
    put(o + 21, m.thickness); put(o + 22, m.attenuationDistance); put3(o + 23, m.attenuationColor);
    // The anisotropy direction goes as (cos, sin), computed here in double, as a material
    // constant would be: a narrow anisotropic lobe turns an in-shader sin and cos with
    // 1e-6 error (SwiftShader, and GPUs' fast transcendentals) into a 1e-3 error in f.
    m.anisotropyRotation = f32(m.anisotropyRotation);
    put(o + 26, m.anisotropy); o[27] = f32(std::cos(m.anisotropyRotation)); o[35] = f32(std::sin(m.anisotropyRotation)); put(o + 28, m.iridescence); put(o + 29, m.iridescenceIor);
    put(o + 30, m.iridescenceThickness); put3(o + 31, m.emissive); put(o + 34, m.emissiveStrength);
    put3(o + 36, c.wo); put3(o + 39, c.wi);
}

std::vector<float> packTables(const pbr::Tables& t) {
    std::vector<float> v;
    for (const auto* g : {&t.gridA(), &t.gridB(), &t.rowAavg(), &t.rowBavg(), &t.gridSh(), &t.gridA4(), &t.gridB4(), &t.gridAavg2(), &t.gridBavg2()})
        for (double x : *g) v.push_back(float(x));
    return v;
}

std::string PbrParity::json() const {
    char b[1024];
    std::snprintf(b, sizeof b,
        "{\n \"check\": \"m3 materials gpu_parity\",\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n \"error\": \"%s\",\n"
        " \"cases\": %u,\n \"seed\": %llu,\n \"bound\": \"|gpu - ref| <= 1e-3 |ref| + 1e-5\",\n \"worst_ratio_to_bound\": %.4f,\n"
        " \"worst_abs\": %.3e,\n \"worst_case\": %d,\n \"worst_family\": %d,\n \"values_outside\": %d,\n"
        " \"control_no_multiple_scattering_worst_ratio\": %.3f,\n \"control_fails\": %s,\n \"pass\": %s\n}\n",
        backend.c_str(), adapter.c_str(), error.c_str(), cases, (unsigned long long)kPbrSeed, worstRatio, worstAbs, worstCase,
        worstFamily, outside, controlWorstRatio, controlWorstRatio > 1.0 ? "true" : "false", pass() ? "true" : "false");
    return b;
}

namespace {
struct Run { std::vector<float> out; std::string err; };
// One dispatch over every case with the given flags; the read-back values.
Run dispatch(rhi::Device& dev, rhi::PipelineHandle pipe, const std::vector<float>& cases, const std::vector<float>& tables,
             std::uint32_t count, std::uint32_t flags) {
    using rhi::BufferUsage; using rhi::Access;
    Run r;
    const std::uint32_t params[4] = {count, flags, 0, 0};
    const std::uint64_t outBytes = std::uint64_t(count) * 6 * 4;
    const rhi::BufferHandle ub = dev.createBuffer({sizeof params, BufferUsage::Uniform | BufferUsage::CopyDst, "pbr params"}, r.err);
    const rhi::BufferHandle cb = dev.createBuffer({cases.size() * 4, BufferUsage::Storage | BufferUsage::CopyDst, "pbr cases"}, r.err);
    const rhi::BufferHandle tb = dev.createBuffer({tables.size() * 4, BufferUsage::Storage | BufferUsage::CopyDst, "pbr tables"}, r.err);
    const rhi::BufferHandle ob = dev.createBuffer({outBytes, BufferUsage::Storage | BufferUsage::CopySrc, "pbr out"}, r.err);
    const rhi::BufferHandle rb = dev.createBuffer({outBytes, BufferUsage::MapRead | BufferUsage::CopyDst, "pbr readback"}, r.err);
    const auto cleanup = [&] { for (auto b : {ub, cb, tb, ob, rb}) if (b.valid()) dev.destroyBuffer(b); };
    rhi::CommandList* cl = r.err.empty() ? dev.begin(r.err) : nullptr;
    if (!cl) { cleanup(); return r; }
    cl->upload(ub, 0, params, sizeof params);
    cl->upload(cb, 0, cases.data(), cases.size() * 4);
    cl->upload(tb, 0, tables.data(), tables.size() * 4);
    const rhi::BufferBarrier in[4] = {{ub, Access::CopyDst, Access::Uniform}, {cb, Access::CopyDst, Access::StorageRead},
                                      {tb, Access::CopyDst, Access::StorageRead}, {ob, Access::Undefined, Access::StorageWrite}};
    cl->barrier(in);
    const rhi::BufferHandle binds[4] = {ub, cb, tb, ob};
    cl->dispatch(pipe, binds, (count + 63) / 64, 1, 1);
    const rhi::BufferBarrier mid[2] = {{ob, Access::StorageWrite, Access::CopySrc}, {rb, Access::Undefined, Access::CopyDst}};
    cl->barrier(mid);
    cl->copyBuffer(ob, 0, rb, 0, outBytes);
    if (!dev.submitAndWait(r.err)) { cleanup(); return r; }
    const auto* px = static_cast<const float*>(dev.mapRead(rb, outBytes, r.err));
    if (px) { r.out.assign(px, px + std::size_t(count) * 6); dev.unmap(rb); }
    cleanup();
    return r;
}
double ratioOf(double gpu, double ref) { return std::fabs(gpu - ref) / (1e-3 * std::fabs(ref) + 1e-5); }
}  // namespace

PbrParity pbrParity(rhi::Device& dev, std::uint32_t count) {
    PbrParity p;
    p.backend = dev.backendName(); p.adapter = dev.adapter().description;
    const rhi::ShaderCode code = gpu_shaders::find("pbr_eval", dev.shaderFormat());
    if (!code.bytes) { p.error = "this build carries no pbr_eval shader"; return p; }
    const auto& L = pbr_passes::kPasses[0];
    const rhi::PipelineHandle pipe = dev.createComputePipeline({L.name, code, std::span(L.binds, std::size_t(L.bindCount))}, p.error);
    if (!pipe.valid()) return p;
    const pbr::Tables tables;
    std::vector<PbrCase> cs = pbrCases(count, kPbrSeed);
    std::vector<float> packed(std::size_t(count) * kPbrCaseFloats);
    for (std::uint32_t k = 0; k < count; ++k) packCase(cs[k], packed.data() + std::size_t(k) * kPbrCaseFloats);
    const std::vector<float> tab = packTables(tables);
    const Run a = dispatch(dev, pipe, packed, tab, count, 0u), c = dispatch(dev, pipe, packed, tab, count, 1u);
    if (a.out.empty() || c.out.empty()) { p.error = !a.err.empty() ? a.err : c.err.empty() ? "no read-back" : c.err; return p; }
    for (std::uint32_t k = 0; k < count; ++k) {
        const PbrCase& x = cs[k];
        const Rgb f = pbr::eval(x.m, tables, x.wo, x.wi), e = pbr::emission(x.m);
        const double ci = std::fabs(x.wi.z), ref[6] = {f.r * ci, f.g * ci, f.b * ci, e.r, e.g, e.b};
        for (int j = 0; j < 6; ++j) {
            const double g = a.out[std::size_t(k) * 6 + j], q = ratioOf(g, ref[j]);
            const char* only = std::getenv("RAW_NATIVE_PBR_PARITY_CASE");   // diagnosis: one case's values, whatever their ratio
            if (!(q <= 1.0) || (only && std::atoi(only) == int(k))) {
                if (!(q <= 1.0)) ++p.outside;
                if (std::getenv("RAW_NATIVE_PBR_PARITY_DUMP") || only)   // diagnosis: every value outside the bound, on stderr
                    std::fprintf(stderr, "case %u family %d value %d: gpu %.9g ref %.9g ratio %.2f wo (%.4f %.4f %.4f) wi (%.4f %.4f %.4f) rough %.4f metal %.3f\n",
                                 k, x.family, j, g, ref[j], q, x.wo.x, x.wo.y, x.wo.z, x.wi.x, x.wi.y, x.wi.z, x.m.roughness, x.m.metallic);
            }
            if (!(q <= p.worstRatio)) {
                p.worstRatio = std::isnan(q) ? 1e30 : q; p.worstAbs = std::fabs(g - ref[j]); p.worstCase = int(k); p.worstFamily = x.family;
            }
            p.controlWorstRatio = std::max(p.controlWorstRatio, ratioOf(c.out[std::size_t(k) * 6 + j], ref[j]));
        }
    }
    p.cases = count;
    return p;
}

}  // namespace raw::gpu_check
