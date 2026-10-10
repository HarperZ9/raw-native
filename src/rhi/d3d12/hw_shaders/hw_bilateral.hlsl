// HW H1.2: a 9 x 9 bilateral filter on RGB radiance (3 floats a pixel). MODE 0: fp32 throughout
// (the reference). MODE 1: fp16 inputs, weights and products, fp32 sum (SM 6.2, native 16-bit).
// Spatial sigma 2 pixels; range weight exp(-(dl / (0.25 (l_p + 1e-4)))^2) on luminance.
cbuffer C : register(b0) { uint W; uint H; };
RWStructuredBuffer<float> src : register(u0);
RWStructuredBuffer<float> dst : register(u1);
#if MODE == 1
typedef half T;
typedef half3 T3;
#else
typedef float T;
typedef float3 T3;
#endif
T3 load(int x, int y) {
    const uint i = 3u * ((uint)clamp(y, 0, (int)H - 1) * W + (uint)clamp(x, 0, (int)W - 1));
    return T3((T)src[i], (T)src[i + 1u], (T)src[i + 2u]);
}
T lumi(T3 c) { return c.x * (T)0.2126f + c.y * (T)0.7152f + c.z * (T)0.0722f; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= W || id.y >= H) { return; }
    const T3 cp = load((int)id.x, (int)id.y);
    const T lp = lumi(cp);
    const T scale = (T)1.0f / ((T)0.25f * (lp + (T)1e-4f));
    float3 acc = float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            const T3 cq = load((int)id.x + dx, (int)id.y + dy);
            const T d = (lumi(cq) - lp) * scale;
            const T w = exp((T)(-(float)(dx * dx + dy * dy) / 8.0f)) * exp(-d * d);
            acc += (float3)(w * cq);
            wsum += (float)w;
        }
    }
    const uint o = 3u * (id.y * W + id.x);
    const float3 r = acc / wsum;
    dst[o] = r.x; dst[o + 1u] = r.y; dst[o + 2u] = r.z;
}
