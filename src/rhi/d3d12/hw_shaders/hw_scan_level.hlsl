// HW H1.2: one level of a full prefix sum. Each group of 64 writes the inclusive scan of its
// 64 inputs to dst and its total to sums[group]. MODE 1 scans with wave intrinsics, MODE 0 with
// the group-shared Hillis-Steele fallback. DROP=1 (the control) reads lane 0's input as 0.
cbuffer C : register(b0) { uint count; };
RWStructuredBuffer<uint> src : register(u0);
RWStructuredBuffer<uint> dst : register(u1);
RWStructuredBuffer<uint> sums : register(u2);
#if MODE == 1
groupshared uint waveTotal[64];
#else
groupshared uint s[2][64];
#endif
[numthreads(64, 1, 1)]
void main(uint gi : SV_GroupIndex, uint3 gid3 : SV_GroupID) {
    // Groups are dispatched as (min(g, 32768), ceil(g / 32768)) to stay under the 65,535 limit.
    const uint g = gid3.y * 32768u + gid3.x;
    const uint i = g * 64u + gi;
    uint v = i < count ? src[i] : 0u;
#if DROP
    if (gi == 0u) { v = 0u; }
#endif
#if MODE == 1
    const uint L = WaveGetLaneCount();
    const uint inclusive = WavePrefixSum(v) + v;
    const uint w = gi / L;
    if (WaveGetLaneIndex() == L - 1u || gi == 63u) { waveTotal[w] = inclusive; }
    GroupMemoryBarrierWithGroupSync();
    uint before = 0u;
    for (uint k = 0u; k < w; k++) { before += waveTotal[k]; }
    const uint r = before + inclusive;
#else
    s[0][gi] = v;
    GroupMemoryBarrierWithGroupSync();
    uint cur = 0u;
    for (uint off = 1u; off < 64u; off <<= 1u) {
        const uint nxt = 1u - cur;
        s[nxt][gi] = s[cur][gi] + (gi >= off ? s[cur][gi - off] : 0u);
        GroupMemoryBarrierWithGroupSync();
        cur = nxt;
    }
    const uint r = s[cur][gi];
#endif
    if (i < count) { dst[i] = r; }
    if (gi == 63u) { sums[g] = r; }
}
