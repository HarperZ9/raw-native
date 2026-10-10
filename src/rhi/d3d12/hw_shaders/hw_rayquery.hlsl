// HW H1.1: closest hits by inline ray query (SM 6.5, DXR tier 1.1). Rays are two float4s
// (origin, tMin) and (direction, tMax); a hit is (t, u, v, primitive index as bits), or
// (-1, 0, 0, -1) on a miss. Opaque, no culling, as the CPU reference.
RWStructuredBuffer<float4> rays : register(u0);
RWStructuredBuffer<float4> hits : register(u1);
RaytracingAccelerationStructure scene : register(t0);
cbuffer C : register(b0) { uint count; };
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= count) { return; }
    const float4 a = rays[2u * id.x];
    const float4 b = rays[2u * id.x + 1u];
    RayDesc r;
    r.Origin = a.xyz; r.TMin = a.w;
    r.Direction = b.xyz; r.TMax = b.w;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, r);
    q.Proceed();
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        const float2 bc = q.CommittedTriangleBarycentrics();
        hits[id.x] = float4(q.CommittedRayT(), bc.x, bc.y, asfloat(q.CommittedPrimitiveIndex()));
    } else {
        hits[id.x] = float4(-1.0f, 0.0f, 0.0f, asfloat(0xFFFFFFFFu));
    }
}
