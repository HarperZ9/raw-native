// HW H1.3: one file, four entry points (evidence/hw-h1-3-bounds.json).
//   vs: vertex pulling, the reference;  as: meshlet culling;  ms: meshlet triangles;  ps: triangle index + 1.
// Both geometry paths share xform(), the pixel shader, the root signature and the raster state.
// CONE_FLIP=1 builds the control amplification shader, whose cone test keeps only what it should drop.
struct Constants {
    float4 vp[4];        // view-projection rows
    float4 planes[6];    // frustum planes, inside when dot(plane.xyz, p) + plane.w >= 0
    float4 eye;          // xyz eye position
    uint meshlets;       // meshlet count
    uint pad0, pad1, pad2;
};
ConstantBuffer<Constants> K : register(b0);
StructuredBuffer<float> positions : register(t0);
struct Meshlet { uint first; uint count; float2 pad; float3 center; float radius; float3 axis; float theta; };
StructuredBuffer<Meshlet> meshlets : register(t1);
RWStructuredBuffer<uint> kept : register(u0);

float4 xform(uint v) {
    const float4 p = float4(positions[3u * v], positions[3u * v + 1u], positions[3u * v + 2u], 1.0f);
    return float4(dot(K.vp[0], p), dot(K.vp[1], p), dot(K.vp[2], p), dot(K.vp[3], p));
}

struct VOut { float4 pos : SV_Position; nointerpolation uint tri : TRI; };
VOut vs(uint vid : SV_VertexID) {
    VOut o;
    o.pos = xform(vid);
    o.tri = vid / 3u;
    return o;
}

// Keep a meshlet when its sphere touches the frustum and its normal cone may show a front face.
// Front faces have normals toward the eye. The cone test is conservative: a meshlet is dropped only
// when every normal within theta of the axis faces away from every point of the sphere, with a
// 0.01 rad margin.
bool visible(Meshlet m) {
    [unroll] for (uint i = 0u; i < 6u; i++) {
        if (dot(K.planes[i].xyz, m.center) + K.planes[i].w < -m.radius * 1.0001f - 1e-4f) { return false; }
    }
    if (m.theta < 0.0f) { return true; }   // normals too spread for a cone
    const float3 v = m.center - K.eye.xyz;
    const float d = length(v);
    if (d <= m.radius) { return true; }
    const float alpha = acos(clamp(dot(v, m.axis) / d, -1.0f, 1.0f));   // angle between view ray and axis
    const float beta = asin(m.radius / d);
    const bool back = alpha + m.theta + beta < 1.5707963f - 0.01f;      // every normal points away
#if CONE_FLIP
    return back;
#else
    return !back;
#endif
}

struct Payload { uint ids[32]; };
groupshared Payload pl;
groupshared uint flags[32];
[numthreads(32, 1, 1)]
void as(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
    const uint m = gid.x * 32u + gtid;
    const uint keep = (m < K.meshlets && visible(meshlets[m])) ? 1u : 0u;
    flags[gtid] = keep;
    GroupMemoryBarrierWithGroupSync();
    uint slot = 0u, total = 0u;   // ordered compaction: survivors keep their order
    for (uint i = 0u; i < 32u; i++) { total += flags[i]; if (i < gtid) { slot += flags[i]; } }
    if (keep != 0u) { pl.ids[slot] = m; }
    if (gtid == 0u && total > 0u) { InterlockedAdd(kept[0], total); }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(total, 1, 1, pl);
}

struct MVert { float4 pos : SV_Position; };
struct MPrim { uint tri : TRI; };
[outputtopology("triangle")]
[numthreads(96, 1, 1)]
void ms(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID, in payload Payload p,
        out vertices MVert verts[96], out indices uint3 tris[32], out primitives MPrim prims[32]) {
    const Meshlet m = meshlets[p.ids[gid.x]];
    SetMeshOutputCounts(3u * m.count, m.count);
    if (gtid < 3u * m.count) { verts[gtid].pos = xform(3u * m.first + gtid); }
    if (gtid < m.count) {
        tris[gtid] = uint3(3u * gtid, 3u * gtid + 1u, 3u * gtid + 2u);
        prims[gtid].tri = m.first + gtid;
    }
}

// The target is cleared to 0, so the shader writes the index plus one; read-back subtracts it again.
uint ps(float4 pos : SV_Position, nointerpolation uint tri : TRI) : SV_Target0 { return tri + 1u; }
