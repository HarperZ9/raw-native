// The texture-identity raster pass in Slang (A5 spike): the same behaviour as
// src/renderer/gpu/shaders/raster.wgsl's texture_identity section.
struct Map {
    float2 size;
    float2 scale;
    float2 offset;
    float2 pad;
};
[[vk::binding(0, 0)]] ConstantBuffer<Map> M : register(b0);
[[vk::binding(1, 0)]] Texture2D<float4> src : register(t0);
[[vk::binding(2, 0)]] SamplerState smp : register(s0);

[shader("vertex")]
float4 vs(uint i : SV_VertexID) : SV_Position {
    float x = float((i << 1u) & 2u);
    float y = float(i & 2u);
    return float4(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fs(float4 q : SV_Position) : SV_Target0 {
    float2 uv = q.xy / M.size * M.scale + M.offset;
    return src.SampleLevel(smp, uv, 0.0);
}
