// HW H1.4: the three async-compute kernels, chosen by MODE. 0: P writes X (salt 0x9E3779B9);
// 1: W writes Y (salt 0x85EBCA6B); both run n rounds of the H1.0 hash. 2: C writes
// Z = X xor rotl(Y, 7) and reads X, so it must run after P.
cbuffer C : register(b0) { uint n; };
RWStructuredBuffer<uint> b0 : register(u0);
#if MODE == 2
RWStructuredBuffer<uint> b1 : register(u1);
RWStructuredBuffer<uint> b2 : register(u2);
#endif
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#if MODE == 2
    const uint y = b1[id.x];
    b2[id.x] = b0[id.x] ^ ((y << 7u) | (y >> 25u));
#else
    uint h = id.x ^ (MODE == 0 ? 0x9E3779B9u : 0x85EBCA6Bu);
    for (uint i = 0u; i < n; i++) { h = h * 1664525u + 1013904223u; h ^= h >> 13u; }
    b0[id.x] = h;
#endif
}
