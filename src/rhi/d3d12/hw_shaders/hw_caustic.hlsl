// Effect C1: spectral caustics through measured glass on DXR inline ray queries (SM 6.6 for the
// 64-bit atomics of the light pass). The float32 mirror of src/renderer/rt_caustic_cpu.cpp and
// rt_caustic_math.hpp: every function keeps the CPU name in snake case and the same operations.
// PASS 0: camera paths, one thread a pixel, samples [begin, begin + count) added into ACC (X, Y, Z
//         sums already weighted by 1 / p(lambda), sample count).
// PASS 1: light paths, one thread a pixel slot, one photon each of round P.round, joined to the
//         camera and added into SPLAT as 64-bit fixed point (2^-32), order independent.
struct CP {
    uint w; uint h; uint begin; uint count;
    uint maxB; uint flags; uint seedLo; uint seedHi;
    uint lights; uint matOff; uint glassOff; uint lightOff;
    float4 eye; float4 fwd; float4 side; float4 up;    // eye.w tan(fovy / 2), fwd.w aspect
    float invTotal; float indexOffset; uint round; uint seedLLo;
    uint seedLHi; uint pad0; uint pad1; uint pad2;
};
ConstantBuffer<CP> P : register(b0);
RWStructuredBuffer<float> T : register(u0);      // 9 floats a triangle
RWStructuredBuffer<float> G : register(u1);      // 12 floats a triangle: shading normals, material
RWStructuredBuffer<float> TB : register(u2);     // tables (rt_caustic_math.hpp)
RWStructuredBuffer<float> ACC : register(u3);    // 4 floats a pixel
RWByteAddressBuffer SPLAT : register(u4);        // 3 uint64 a pixel
RaytracingAccelerationStructure SCENE : register(t0);

static const float PI = 3.14159265f;
static const float L0 = 380.0f;
static const float LSPAN = 400.0f;
static const uint K_CMF = 0u;
static const uint K_ILLUM = 243u;
static const uint F_SPLIT = 1u;
static const uint F_NODISP = 2u;
static const uint F_DROPCOS = 4u;
static const uint F_FIRST = 8u;

uint lowbias32(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
float2 rand2(uint seedLo, uint seedHi, uint px, uint smp, uint dim) {
    const uint h = lowbias32(px ^ lowbias32(smp ^ lowbias32(dim ^ seedLo ^ lowbias32(seedHi))));
    const uint g = lowbias32(h ^ 0x9E3779B9u);
    return float2(float(h >> 8u), float(g >> 8u)) * 5.96046448e-8f;
}
float table_at(uint base, uint stride, uint k, float lambda) {
    const float x = min(max((lambda - L0) / 5.0f, 0.0f), 80.0f);
    const uint i = uint(min(floor(x), 79.0f));
    const float f = x - float(i);
    return TB[base + i * stride + k] * (1.0f - f) + TB[base + (i + 1u) * stride + k] * f;
}
float sellmeier(uint g, float lambdaNm) {
    const float l2 = (lambdaNm * 1e-3f) * (lambdaNm * 1e-3f);
    const float n2 = 1.0f + TB[g] * l2 / (l2 - TB[g + 3u]) + TB[g + 1u] * l2 / (l2 - TB[g + 4u]) + TB[g + 2u] * l2 / (l2 - TB[g + 5u]);
    return sqrt(n2);
}
float index_of(uint mat, float lambda) {
    const uint g = P.glassOff + uint(TB[P.matOff + mat * 4u + 2u]) * 8u;
    return (((P.flags & F_NODISP) != 0u) ? TB[g + 6u] : sellmeier(g, lambda)) + P.indexOffset;
}
float fresnel(float cosI, float eta) {
    const float s2 = (1.0f - cosI * cosI) / (eta * eta);
    if (s2 >= 1.0f) { return 1.0f; }
    const float cosT = sqrt(1.0f - s2);
    const float rs = (cosI - eta * cosT) / (cosI + eta * cosT);
    const float rp = (eta * cosI - cosT) / (eta * cosI + cosT);
    return 0.5f * (rs * rs + rp * rp);
}
float3 offset_p(float3 p, float3 ng, float3 dir) {
    const float e = 2e-4f * (1.0f + max(abs(p.x), max(abs(p.y), abs(p.z))));
    return p + ng * ((dot(ng, dir) > 0.0f) ? e : -e);
}
float3 glass_step(float3 d, float3 ns, float3 ng, float n, float u) {
    const bool entering = dot(ng, d) < 0.0f;
    const float eta = entering ? n : 1.0f / n;
    float3 m = (dot(ns, d) < 0.0f) ? ns : -ns;
    float cosI = -dot(m, d);
    if (!(cosI > 0.0f)) { m = (dot(ng, d) < 0.0f) ? ng : -ng; cosI = -dot(m, d); }
    const float F = fresnel(cosI, eta);
    if (u < F) { return normalize(d + m * (2.0f * cosI)); }
    const float cosT = sqrt(max(0.0f, 1.0f - (1.0f - cosI * cosI) / (eta * eta)));
    return normalize(d * (1.0f / eta) + m * (cosI / eta - cosT));
}
float3 basis_t(float3 n) {
    const float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    return float3(1.0f + s * n.x * n.x * a, s * n.x * n.y * a, -s * n.x);
}
float3 basis_b(float3 n) {
    const float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    return float3(n.x * n.y * a, s + n.y * n.y * a, -n.y);
}
float3 cosine_dir(float3 n, float u1, float u2) {
    const float r = sqrt(u1), phi = 2.0f * PI * u2, z = sqrt(max(0.0f, 1.0f - u1));
    return normalize(basis_t(n) * (r * cos(phi)) + basis_b(n) * (r * sin(phi)) + n * z);
}
struct Surf { bool hit; float t; float3 p; float3 ng; float3 ns; uint mat; };
Surf closest(float3 o, float3 d) {
    Surf s; s.hit = false; s.t = 0.0f; s.p = o; s.ng = float3(0, 0, 1); s.ns = s.ng; s.mat = 0u;
    RayDesc r; r.Origin = o; r.Direction = d; r.TMin = 1e-4f; r.TMax = 3.0e38f;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(SCENE, RAY_FLAG_NONE, 0xFF, r);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) { return s; }
    const uint t = q.CommittedPrimitiveIndex();
    const float2 bc = q.CommittedTriangleBarycentrics();
    const float3 a = float3(T[t * 9u], T[t * 9u + 1u], T[t * 9u + 2u]);
    const float3 b = float3(T[t * 9u + 3u], T[t * 9u + 4u], T[t * 9u + 5u]);
    const float3 c = float3(T[t * 9u + 6u], T[t * 9u + 7u], T[t * 9u + 8u]);
    const uint g = t * 12u;
    const float w = 1.0f - bc.x - bc.y;
    s.hit = true; s.t = q.CommittedRayT(); s.p = o + d * s.t;
    s.ng = normalize(cross(b - a, c - a));
    s.ns = normalize(float3(G[g], G[g + 1u], G[g + 2u]) * w + float3(G[g + 3u], G[g + 4u], G[g + 5u]) * bc.x + float3(G[g + 6u], G[g + 7u], G[g + 8u]) * bc.y);
    s.mat = uint(G[g + 9u]);
    return s;
}
bool occluded(float3 o, float3 d, float tMax) {
    RayDesc r; r.Origin = o; r.Direction = d; r.TMin = 1e-4f; r.TMax = tMax;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(SCENE, RAY_FLAG_NONE, 0xFF, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
uint lb(uint l) { return P.lightOff + l * 16u; }
float lamp_le(uint l, float3 w, float lambda) {
    const uint L = lb(l);
    const float cs = TB[L + 9u] * w.x + TB[L + 10u] * w.y + TB[L + 11u] * w.z;
    return (cs > 0.0f && cs >= TB[L + 13u]) ? TB[L + 15u] * table_at(K_ILLUM, 1u, 0u, lambda) : 0.0f;
}
float3 lamp_point(uint L, float2 u) {
    return float3(TB[L], TB[L + 1u], TB[L + 2u]) + float3(TB[L + 3u], TB[L + 4u], TB[L + 5u]) * u.x + float3(TB[L + 6u], TB[L + 7u], TB[L + 8u]) * u.y;
}
float nee(Surf s, float3 n, float lambda, float2 u, float pick) {
    const uint l = min(uint(pick * float(P.lights)), P.lights - 1u);
    const uint L = lb(l);
    const float3 to = lamp_point(L, u) - s.p;
    const float dist = length(to);
    const float3 wl = to * (1.0f / dist);
    const float cosX = dot(n, wl);
    const float cosL = -(TB[L + 9u] * wl.x + TB[L + 10u] * wl.y + TB[L + 11u] * wl.z);
    if (!(cosX > 0.0f) || !(cosL > 0.0f)) { return 0.0f; }
    const float le = lamp_le(l, -wl, lambda);
    if (le <= 0.0f || occluded(offset_p(s.p, n, wl), wl, dist * (1.0f - 1e-3f))) { return 0.0f; }
    return le * cosX * cosL / (dist * dist) * TB[L + 12u] * float(P.lights);
}
float3 cam_dir(float px, float py) {
    const float nx = px / float(P.w) * 2.0f - 1.0f, ny = 1.0f - py / float(P.h) * 2.0f;
    return normalize(P.fwd.xyz + P.side.xyz * (nx * P.eye.w * P.fwd.w) + P.up.xyz * (ny * P.eye.w));
}
float camera_sample(uint pix, uint x, uint y, uint smp, out float lambda) {
    const float2 j = rand2(P.seedLo, P.seedHi, pix, smp, 0u);
    const float2 rl = rand2(P.seedLo, P.seedHi, pix, smp, 63u);
    lambda = L0 + LSPAN * rl.x;
    float3 ro = P.eye.xyz;
    float3 rd = cam_dir(float(x) + j.x, float(y) + j.y);
    const bool split = (P.flags & F_SPLIT) != 0u;
    float tp = 1.0f, L = 0.0f;
    bool diffuseSeen = false;
    for (uint b = 0u; b <= P.maxB; b++) {
        const Surf s = closest(ro, rd);
        if (!s.hit) { break; }
        const float kind = TB[P.matOff + s.mat * 4u];
        const float3 ng = (dot(s.ng, rd) < 0.0f) ? s.ng : -s.ng;
        if (kind == 2.0f) {
            if (!split || !diffuseSeen) { L += tp * lamp_le(uint(TB[P.matOff + s.mat * 4u + 3u]), -rd, lambda); }
            break;
        }
        const float2 u = rand2(P.seedLo, P.seedHi, pix, smp, 2u + 3u * b);
        const float2 v = rand2(P.seedLo, P.seedHi, pix, smp, 3u + 3u * b);
        const float2 w = rand2(P.seedLo, P.seedHi, pix, smp, 4u + 3u * b);
        if (kind == 1.0f) {
            const float3 nd = glass_step(rd, s.ns, s.ng, index_of(s.mat, lambda), u.x);
            ro = offset_p(s.p, s.ng, nd); rd = nd;
            continue;
        }
        const float rho = TB[P.matOff + s.mat * 4u + 1u];
        if (split) { L += tp * (rho / PI) * nee(s, ng, lambda, v, w.x); }
        diffuseSeen = true;
        const float3 wi = cosine_dir(ng, u.x, u.y);
        tp *= rho;
        if (b >= 3u) {
            const float qq = min(0.95f, tp);
            if (w.y >= qq) { break; }
            tp /= qq;
        }
        ro = offset_p(s.p, ng, wi); rd = wi;
    }
    return L;
}
void splat(uint px, float v, float lambda) {
    for (uint ch = 0u; ch < 3u; ch++) {
        const float x = v * table_at(K_CMF, 3u, ch, lambda) * 4294967296.0f;
        if (x >= 0.5f) { SPLAT.InterlockedAdd64((px * 3u + ch) * 8u, (uint64_t)(x + 0.5f)); }
    }
}
void light_path(uint k) {
    const uint sl = P.seedLLo, sh = P.seedLHi, rd0 = P.round;
    const float2 r0 = rand2(sl, sh, k, rd0, 0u), r1 = rand2(sl, sh, k, rd0, 1u), r2 = rand2(sl, sh, k, rd0, 2u);
    const uint l = min(uint(r0.x * float(P.lights)), P.lights - 1u);
    const float lambda = L0 + LSPAN * rand2(sl, sh, k, rd0, 63u).x;
    const uint L = lb(l);
    const float3 nl = float3(TB[L + 9u], TB[L + 10u], TB[L + 11u]);
    const float3 q = lamp_point(L, r1);
    const float s2 = r2.x * TB[L + 14u], phi = 2.0f * PI * r2.y, ct = sqrt(1.0f - s2), st = sqrt(s2);
    float3 rd = normalize(basis_t(nl) * (st * cos(phi)) + basis_b(nl) * (st * sin(phi)) + nl * ct);
    float3 ro = offset_p(q, nl, rd);
    float beta = TB[L + 15u] * table_at(K_ILLUM, 1u, 0u, lambda) * TB[L + 12u] * PI * TB[L + 14u] * float(P.lights) * LSPAN;
    const float apix = (2.0f * P.eye.w * P.fwd.w / float(P.w)) * (2.0f * P.eye.w / float(P.h));
    bool sawGlass = false;
    for (uint b = 0u; b <= P.maxB; b++) {
        const Surf s = closest(ro, rd);
        if (!s.hit) { break; }
        const float kind = TB[P.matOff + s.mat * 4u];
        if (kind == 2.0f) { break; }
        const float2 u = rand2(sl, sh, k, rd0, 3u + 3u * b), v = rand2(sl, sh, k, rd0, 4u + 3u * b);
        if (kind == 1.0f) {
            const float3 nd = glass_step(rd, s.ns, s.ng, index_of(s.mat, lambda), u.x);
            ro = offset_p(s.p, s.ng, nd); rd = nd; sawGlass = true;
            continue;
        }
        if (!sawGlass) { break; }
        const float3 ng = (dot(s.ng, rd) < 0.0f) ? s.ng : -s.ng;
        const float rho = TB[P.matOff + s.mat * 4u + 1u];
        const float3 tc = P.eye.xyz - s.p;
        const float dist = length(tc);
        const float3 wc = tc * (1.0f / dist);
        const float cosX = dot(ng, wc), z = -dot(wc, P.fwd.xyz);
        if (cosX > 0.0f && z > 0.0f) {
            const float nx = -dot(wc, P.side.xyz) / (z * P.eye.w * P.fwd.w), ny = -dot(wc, P.up.xyz) / (z * P.eye.w);
            const float fx = (nx * 0.5f + 0.5f) * float(P.w), fy = (0.5f - ny * 0.5f) * float(P.h);
            if (fx >= 0.0f && fy >= 0.0f && fx < float(P.w) && fy < float(P.h) && !occluded(offset_p(s.p, ng, wc), wc, dist * (1.0f - 1e-3f))) {
                const float cs = ((P.flags & F_DROPCOS) != 0u) ? 1.0f : cosX;
                const float v0 = beta * (rho / PI) * cs / (dist * dist * z * z * z * apix) * P.invTotal;
                splat(uint(int(fy)) * P.w + uint(int(fx)), v0, lambda);
            }
        }
        const float3 wi = cosine_dir(ng, u.x, u.y);
        beta *= rho;
        if (b >= 3u) {
            const float qq = min(0.95f, rho);
            if (v.y >= qq) { break; }
            beta /= qq;
        }
        ro = offset_p(s.p, ng, wi); rd = wi;
    }
}
[numthreads(8, 8, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    const uint pix = gid.y * P.w + gid.x;
#if PASS == 0
    const uint o = pix * 4u;
    if ((P.flags & F_FIRST) != 0u) { ACC[o] = 0.0f; ACC[o + 1u] = 0.0f; ACC[o + 2u] = 0.0f; ACC[o + 3u] = 0.0f; }
    for (uint k = 0u; k < P.count; k++) {
        float lambda;
        const float L = camera_sample(pix, gid.x, gid.y, P.begin + k, lambda) * LSPAN;
        for (uint ch = 0u; ch < 3u; ch++) { ACC[o + ch] = ACC[o + ch] + L * table_at(K_CMF, 3u, ch, lambda); }
        ACC[o + 3u] = ACC[o + 3u] + 1.0f;
    }
#else
    light_path(pix);
#endif
}
