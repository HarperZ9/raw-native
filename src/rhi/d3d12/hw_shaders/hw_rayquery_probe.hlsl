// HW H1.0: the smallest inline ray query (SM 6.5, DXR tier 1.1). Only pipeline creation is
// checked in H1.0; H1.1 dispatches real queries against the CPU BVH.
RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<float> dst : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    RayDesc r;
    r.Origin = float3(0.0f, 0.0f, -1.0f);
    r.Direction = float3(0.0f, 0.0f, 1.0f);
    r.TMin = 0.0f;
    r.TMax = 10.0f;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, r);
    q.Proceed();
    dst[id.x] = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0f;
}
