// The fallback of hw_half.hlsl at 32 bits (SM 6.0). The products and sums of the test
// triples are exact in both widths; the flag reads 0 because 2048 + 1 is exact at 32 bits.
RWStructuredBuffer<float> src : register(u0);
RWStructuredBuffer<float> dst : register(u1);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const float a = src[3u * id.x];
    const float b = src[3u * id.x + 1u];
    const float c = src[3u * id.x + 2u];
    dst[id.x + 1u] = a * b + c;
    if (id.x == 0u) {
        const float big = src[0] * 0.0f + 2048.0f;
        const float one = src[1] * 0.0f + 1.0f;
        dst[0] = ((big + one) == big) ? 1.0f : 0.0f;
    }
}
