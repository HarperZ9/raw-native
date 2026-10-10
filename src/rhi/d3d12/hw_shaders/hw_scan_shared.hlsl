// The fallback of hw_scan_wave.hlsl: the same inclusive prefix sum per group of 64 threads,
// by a Hillis-Steele scan in group-shared memory (SM 6.0, no wave intrinsics).
RWStructuredBuffer<uint> src : register(u0);
RWStructuredBuffer<uint> dst : register(u1);
RWStructuredBuffer<uint> lanes : register(u2);
groupshared uint s[2][64];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    s[0][gi] = src[id.x];
    GroupMemoryBarrierWithGroupSync();
    uint cur = 0u;
    for (uint off = 1u; off < 64u; off <<= 1u) {
        const uint nxt = 1u - cur;
        s[nxt][gi] = s[cur][gi] + (gi >= off ? s[cur][gi - off] : 0u);
        GroupMemoryBarrierWithGroupSync();
        cur = nxt;
    }
    dst[id.x] = s[cur][gi];
    lanes[id.x] = 0u;
}
