// HW H1.2: add the scanned totals of the groups before each element's group (level offsets).
cbuffer C : register(b0) { uint count; };
RWStructuredBuffer<uint> dst : register(u0);
RWStructuredBuffer<uint> scanned : register(u1);
[numthreads(64, 1, 1)]
void main(uint gi : SV_GroupIndex, uint3 gid3 : SV_GroupID) {
    const uint g = gid3.y * 32768u + gid3.x;   // as in hw_scan_level.hlsl
    const uint i = g * 64u + gi;
    if (i < count && g > 0u) { dst[i] += scanned[g - 1u]; }
}
