// The presentation host's passes (N2, evidence/n2-presentation-bounds.json). Written for
// raw-native; no third-party source. One full-screen triangle drives every pass.
//   vs        the full-screen triangle
//   scene_fs  the test scene: integer arithmetic only, so a pixel does not depend on float rounding
//   blit_fs   a texel Load at pixel + offset from the offscreen texture (the control moves offset)
//   pq_fs     SMPTE ST 2084 encode of linear Rec.2020 light (1.0 = 100 nits) into an HDR10 target;
//             PQ_CONTROL=1 uses m1 = 2610 / 4096, M1's wrong-exponent control

float4 vs(uint i : SV_VertexID) : SV_Position {
    const float x = float((i << 1u) & 2u);
    const float y = float(i & 2u);
    return float4(x * 2.0f - 1.0f, 1.0f - y * 2.0f, 0.0f, 1.0f);
}

struct Scene { uint width; uint height; uint frame; uint seed; };
ConstantBuffer<Scene> S : register(b0);

uint mix32(uint x){
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float4 scene_fs(float4 q : SV_Position) : SV_Target0 {
    const uint2 p = uint2(q.xy);
    const uint f = S.frame;
    const uint r = (p.x * 255u) / max(S.width - 1u, 1u);
    const uint g = (p.y * 255u) / max(S.height - 1u, 1u);
    const uint bar = ((p.x + f * 3u) / 16u) & 1u;
    const uint diag = ((p.x + p.y + f * 2u) % 64u) < 8u ? 1u : 0u;
    const uint n = mix32(p.x + p.y * 4096u + f * 16777619u + S.seed) & 63u;
    const uint3 c = uint3((r + n) & 255u, (g ^ (f & 255u)) & 255u, (bar * 160u + diag * 95u + (n >> 2)) & 255u);
    return float4(float3(c) / 255.0f, 1.0f);
}

struct Blit { int2 offset; uint2 pad; };
ConstantBuffer<Blit> B : register(b0);
Texture2D<float4> src : register(t0);

float4 blit_fs(float4 q : SV_Position) : SV_Target0 {
    return src.Load(int3(int2(q.xy) + B.offset, 0));
}

struct Pq { uint width; uint count; uint pad0; uint pad1; };
ConstantBuffer<Pq> P : register(b0);
RWStructuredBuffer<float4> L : register(u0);

float pqEncode(float linear100){
#if PQ_CONTROL
    const float m1 = 2610.0f / 4096.0f;
#else
    const float m1 = 2610.0f / 16384.0f;
#endif
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    const float y = max(linear100, 0.0f) / 100.0f;
    const float p = pow(y, m1);
    return pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
}

float4 pq_fs(float4 q : SV_Position) : SV_Target0 {
    const uint2 p = uint2(q.xy);
    const uint i = p.y * P.width + p.x;
    if (i >= P.count) return float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float3 v = L[i].rgb;
    return float4(pqEncode(v.r), pqEncode(v.g), pqEncode(v.b), 1.0f);
}
