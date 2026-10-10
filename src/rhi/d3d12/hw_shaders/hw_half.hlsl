// HW H1.0: native 16-bit arithmetic (SM 6.2, compiled with -enable-16bit-types).
// u0 holds triples (a, b, c) as floats exactly representable in half; u1 receives a * b + c
// computed in half, and one flag: 1 when half(2048) + half(1) rounds to 2048 (true 16-bit
// storage), 0 when the arithmetic ran at 32 bits. The values 2048 and 1 come from the buffer,
// so the compiler cannot fold them.
RWStructuredBuffer<float> src : register(u0);
RWStructuredBuffer<float> dst : register(u1);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const half a = (half)src[3u * id.x];
    const half b = (half)src[3u * id.x + 1u];
    const half c = (half)src[3u * id.x + 2u];
    const half r = a * b + c;
    dst[id.x + 1u] = (float)r;
    if (id.x == 0u) {
        const half big = (half)(src[0] * 0.0f + 2048.0f);
        const half one = (half)(src[1] * 0.0f + 1.0f);
        dst[0] = ((big + one) == big) ? 1.0f : 0.0f;
    }
}
