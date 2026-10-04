#include "frame_pack.hpp"
#include "raw/reconcile.hpp"
#include <cstring>
#include <limits>
namespace raw::gpu_host {
std::vector<float> packTriangles(const Scene& scene){
    std::vector<float> tris;
    for (const Mesh& m : scene.meshes)
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3){
            float t[24] = {};
            for (int k = 0; k < 3; ++k){
                Vec3 p = m.positions[m.indices[i + k]], n = m.normals[m.indices[i + k]];
                t[3*k] = p.x; t[3*k+1] = p.y; t[3*k+2] = p.z;
                t[9+3*k] = n.x; t[9+3*k+1] = n.y; t[9+3*k+2] = n.z;
            }
            t[18] = m.material.albedo.x; t[19] = m.material.albedo.y; t[20] = m.material.albedo.z;
            tris.insert(tris.end(), t, t + 24);
        }
    return tris;
}
void packParams(float pf[48], const Scene& scene, const Mat4& vp, const Mat4& prevVP, int w, int h,
                uint32_t ntri, bool rtao){
    std::memcpy(pf, vp.m, 64); std::memcpy(pf + 16, prevVP.m, 64);
    Vec3 ld = scene.lights.empty() ? Vec3{0,-1,0} : scene.lights[0].dir;
    pf[32] = ld.x; pf[33] = ld.y; pf[34] = ld.z; pf[35] = scene.lights.empty() ? 1.0f : scene.lights[0].intensity;
    pf[36] = kAoRadius; pf[37] = 6.0f; pf[38] = 0; pf[39] = 0;
    uint32_t pu[8] = {(uint32_t)w, (uint32_t)h, ntri, (uint32_t)kRtSamples, (uint32_t)kSsSamples, rtao ? 1u : 0u, 0, 0};
    std::memcpy(pf + 40, pu, 32);
}
uint64_t readBytes(int i, int w, int h, bool rtao, bool frameOnly){
    if (i != kFrameChannel && frameOnly) return 0;
    if (i == 6 && !rtao) return 0;
    return (uint64_t)w * (uint64_t)h * kChannelBytes[i];
}
void unpackFrame(FrameResult& r, const float* const f[kChannels], int w, int h, const Mat4& vp,
                 const RenderOptions& opts, bool frameOnly){
    const uint32_t N = (uint32_t)w * (uint32_t)h;
    const bool rtao = opts.rtao;
    r = FrameResult();
    r.frame.resize(w, h);
    for (uint32_t i = 0; i < N; ++i) r.frame.px[i] = {f[7][4*i], f[7][4*i+1], f[7][4*i+2]};
    if (frameOnly) return;
    r.g.resize(w, h); r.aoSS.resize(w, h); r.hdr.resize(w, h);
    if (rtao) r.aoRT.resize(w, h);
    for (uint32_t i = 0; i < N; ++i){
        const bool cov = f[3][4*i+3] != 0.0f;
        r.g.depth.px[i]    = cov ? f[0][i] : std::numeric_limits<float>::infinity();
        r.g.position.px[i] = {f[1][4*i], f[1][4*i+1], f[1][4*i+2]};
        r.g.normal.px[i]   = {f[2][4*i], f[2][4*i+1], f[2][4*i+2]};
        r.g.albedo.px[i]   = {f[3][4*i], f[3][4*i+1], f[3][4*i+2]};
        r.g.mask.px[i]     = cov ? 1 : 0;
        r.g.motion.px[i]   = {f[4][4*i], f[4][4*i+1]};
        r.motionValid += f[4][4*i+3] != 0.0f; r.motionTotal += cov;
        r.aoSS.px[i] = f[5][i];
        if (rtao) r.aoRT.px[i] = f[6][i];
        r.hdr.px[i]  = {f[8][4*i], f[8][4*i+1], f[8][4*i+2]};
    }
    if (rtao) r.rec = reconcile(r.aoSS, r.aoRT, r.g.mask, opts.tolerance);
    else r.rec.errorMap.resize(w, h);
    r.viewProj = vp;
}
}
