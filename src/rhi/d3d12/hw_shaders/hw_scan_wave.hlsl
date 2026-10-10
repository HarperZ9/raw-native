// HW H1.0 and H1.2: inclusive prefix sum over each group of 64 threads, built from wave
// intrinsics (SM 6.0). Waves are taken to be consecutive thread indices within the group;
// if a driver forms them otherwise the CPU comparison fails, which is the point.
// u0 input, u1 output, u2 per-thread lane count (reported, so the CPU knows the wave size).
RWStructuredBuffer<uint> src : register(u0);
RWStructuredBuffer<uint> dst : register(u1);
RWStructuredBuffer<uint> lanes : register(u2);
groupshared uint waveTotal[64];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    const uint v = src[id.x];
    const uint L = WaveGetLaneCount();
    const uint inclusive = WavePrefixSum(v) + v;
    const uint w = gi / L;
    if (WaveGetLaneIndex() == L - 1u || gi == 63u) { waveTotal[w] = inclusive; }
    GroupMemoryBarrierWithGroupSync();
    uint before = 0u;
    for (uint k = 0u; k < w; k++) { before += waveTotal[k]; }
    dst[id.x] = before + inclusive;
    lanes[id.x] = L;
}
