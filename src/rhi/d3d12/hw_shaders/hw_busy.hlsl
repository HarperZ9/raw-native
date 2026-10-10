// HW H1.0: a busy kernel for the timestamp linearity check. Each thread runs n rounds of an
// integer hash; root constant b0.x is n. Built twice: FLAT=1 ignores n (fixed 4,096 rounds),
// the control that must fail the linearity bound.
cbuffer C : register(b0) { uint n; };
RWStructuredBuffer<uint> dst : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#if FLAT
    const uint rounds = 4096u + (n & 0u);
#else
    const uint rounds = n;
#endif
    uint h = id.x;
    for (uint i = 0u; i < rounds; i++) { h = h * 1664525u + 1013904223u; h ^= h >> 13u; }
    dst[id.x] = h;
}
