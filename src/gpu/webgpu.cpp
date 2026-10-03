// The WebGPU backend, through Emscripten's WebGPU port (emdawnwebgpu) and WGSL
// compute shaders. Waits use wgpuInstanceWaitAny with JSPI, so the render reads
// as straight-line code and the CLI's main() stays synchronous in shape.
#include "raw/gpu.hpp"
#include "raw/reconcile.hpp"
#include "raw_gpu_shaders.hpp"   // generated: kCommonWgsl, kPassesWgsl
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
namespace raw {
namespace {
struct Ctx {
    WGPUInstance inst{}; WGPUAdapter adapter{}; WGPUDevice device{}; WGPUQueue queue{};
    std::map<std::string, WGPUComputePipeline> pipes;
    GpuAdapterInfo info; std::string error; bool tried{false}, ok{false};
};
Ctx& ctx(){ static Ctx c; return c; }
WGPUStringView sv(const char* s){ return WGPUStringView{s, WGPU_STRLEN}; }
std::string str(WGPUStringView v){ return v.data ? std::string(v.data, v.length == WGPU_STRLEN ? std::strlen(v.data) : v.length) : std::string(); }
void waitFor(WGPUFuture f){ WGPUFutureWaitInfo w{f, false}; wgpuInstanceWaitAny(ctx().inst, 1, &w, UINT64_MAX); }
const char* backendName(WGPUBackendType b){
    switch (b){ case WGPUBackendType_WebGPU: return "webgpu"; case WGPUBackendType_D3D12: return "d3d12";
        case WGPUBackendType_D3D11: return "d3d11"; case WGPUBackendType_Vulkan: return "vulkan";
        case WGPUBackendType_Metal: return "metal"; default: return ""; }
}
// Split passes.wgsl at its "//@pass <name>" markers and prepend the shared code.
std::string passSource(const std::string& name){
    const std::string all = kPassesWgsl, mark = "//@pass " + name + "\n";
    size_t a = all.find(mark);
    if (a == std::string::npos) return {};
    size_t b = all.find("//@pass ", a + mark.size());
    return std::string(kCommonWgsl) + "\n" + all.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
bool popScope(const char* what, std::string& err){
    struct R { bool bad{false}; std::string msg; } r;
    WGPUPopErrorScopeCallbackInfo cb = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_WaitAnyOnly; cb.userdata1 = &r;
    cb.callback = [](WGPUPopErrorScopeStatus s, WGPUErrorType t, WGPUStringView m, void* u, void*){
        auto* r = (R*)u; if (s != WGPUPopErrorScopeStatus_Success || t != WGPUErrorType_NoError){ r->bad = true; r->msg = str(m); } };
    waitFor(wgpuDevicePopErrorScope(ctx().device, cb));
    if (r.bad){ err = std::string(what) + ": " + r.msg; return false; }
    return true;
}
WGPUComputePipeline pipeline(const std::string& name, std::string& err){
    Ctx& c = ctx();
    if (auto it = c.pipes.find(name); it != c.pipes.end()) return it->second;
    std::string src = passSource(name);
    wgpuDevicePushErrorScope(c.device, WGPUErrorFilter_Validation);
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = WGPUStringView{src.data(), src.size()};
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    md.nextInChain = &wgsl.chain;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(c.device, &md);
    WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pd.compute.module = mod; pd.compute.entryPoint = sv("main");
    WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(c.device, &pd);
    wgpuShaderModuleRelease(mod);
    if (!popScope(("pass " + name).c_str(), err)) return nullptr;
    c.pipes[name] = p;
    return p;
}
WGPUBuffer buffer(uint64_t size, WGPUBufferUsage usage){
    WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
    d.size = (size + 3) & ~uint64_t(3); d.usage = usage;
    return wgpuDeviceCreateBuffer(ctx().device, &d);
}
struct Pass { const char* name; std::vector<WGPUBuffer> binds; uint32_t gx, gy; };
}  // namespace

bool gpuCompiled(){ return true; }

bool gpuInit(GpuAdapterInfo& info, std::string& err){
    Ctx& c = ctx();
    if (c.tried){ info = c.info; err = c.error; return c.ok; }
    c.tried = true;
    WGPUInstanceFeatureName feat = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    id.requiredFeatureCount = 1; id.requiredFeatures = &feat;
    c.inst = wgpuCreateInstance(&id);
    if (!c.inst){ c.error = "no WebGPU instance (navigator.gpu is absent)"; err = c.error; return false; }
    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo acb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    acb.mode = WGPUCallbackMode_WaitAnyOnly; acb.userdata1 = &c;
    acb.callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void* u, void*){
        auto* c = (Ctx*)u; if (s == WGPURequestAdapterStatus_Success) c->adapter = a; else c->error = "no adapter: " + str(m); };
    waitFor(wgpuInstanceRequestAdapter(c.inst, &ao, acb));
    if (!c.adapter){ if (c.error.empty()) c.error = "no adapter"; err = c.error; return false; }
    WGPUAdapterInfo ai = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(c.adapter, &ai) == WGPUStatus_Success){
        c.info = GpuAdapterInfo{str(ai.vendor), str(ai.architecture), str(ai.device), str(ai.description), backendName(ai.backendType)};
        wgpuAdapterInfoFreeMembers(ai);
    }
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView m, void*, void*){
        std::fprintf(stderr, "webgpu error: %s\n", str(m).c_str()); };
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView m, void*, void*){
        std::fprintf(stderr, "webgpu device lost: %s\n", str(m).c_str()); };
    WGPURequestDeviceCallbackInfo dcb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dcb.mode = WGPUCallbackMode_WaitAnyOnly; dcb.userdata1 = &c;
    dcb.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void* u, void*){
        auto* c = (Ctx*)u; if (s == WGPURequestDeviceStatus_Success) c->device = d; else c->error = "no device: " + str(m); };
    waitFor(wgpuAdapterRequestDevice(c.adapter, &dd, dcb));
    if (!c.device){ if (c.error.empty()) c.error = "no device"; err = c.error; return false; }
    c.queue = wgpuDeviceGetQueue(c.device);
    c.ok = true; info = c.info;
    return true;
}

namespace {
// Triangles in scene order, 24 floats each: positions, normals, albedo.
std::vector<float> packTriangles(const Scene& scene){
    std::vector<float> tris;
    for (const Mesh& m : scene.meshes)
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3){
            float t[24] = {};
            for (int k = 0; k < 3; ++k){
                Vec3 p = m.positions[m.indices[i + k]], n = m.normals[m.indices[i + k]];
                t[3*k] = p.x; t[3*k+1] = p.y; t[3*k+2] = p.z;
                t[9+3*k] = n.x; t[9+3*k+1] = n.y; t[9+3*k+2] = n.z;
            }
            t[18] = m.material.albedo.x; t[19] = m.material.albedo.y; t[20] = m.material.albedo.z;
            tris.insert(tris.end(), t, t + 24);
        }
    return tris;
}
// The WGSL Params struct, 192 bytes: two matrices, light, misc, eight u32.
void packParams(float pf[48], const Scene& scene, const Mat4& vp, const Mat4& prevVP, int w, int h,
                uint32_t ntri, bool rtao){
    std::memcpy(pf, vp.m, 64); std::memcpy(pf + 16, prevVP.m, 64);
    Vec3 ld = scene.lights.empty() ? Vec3{0,-1,0} : scene.lights[0].dir;
    pf[32] = ld.x; pf[33] = ld.y; pf[34] = ld.z; pf[35] = scene.lights.empty() ? 1.0f : scene.lights[0].intensity;
    pf[36] = kAoRadius; pf[37] = 6.0f; pf[38] = 0; pf[39] = 0;
    uint32_t pu[8] = {(uint32_t)w, (uint32_t)h, ntri, (uint32_t)kRtSamples, (uint32_t)kSsSamples, rtao ? 1u : 0u, 0, 0};
    std::memcpy(pf + 40, pu, 32);
}
// Every buffer one render uses, released together.
struct Frame {
    WGPUBuffer P{}, T{}, SF{}, SI{}, depth{}, pos{}, nrm{}, am{}, mot{}, ss{}, rt{}, frame{}, hdr{}, stage{};
    ~Frame(){ for (WGPUBuffer b : {P, T, SF, SI, depth, pos, nrm, am, mot, ss, rt, frame, hdr, stage})
                  if (b){ wgpuBufferDestroy(b); wgpuBufferRelease(b); } }
};
bool encodePass(WGPUCommandEncoder enc, const Pass& ps, std::vector<WGPUBindGroup>& groups, std::string& err){
    WGPUComputePipeline pipe = pipeline(ps.name, err);
    if (!pipe) return false;
    std::vector<WGPUBindGroupEntry> e(ps.binds.size());
    for (size_t i = 0; i < e.size(); ++i){
        e[i] = WGPU_BIND_GROUP_ENTRY_INIT; e[i].binding = (uint32_t)i; e[i].buffer = ps.binds[i]; e[i].size = WGPU_WHOLE_SIZE; }
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipe, 0);
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bd.layout = layout; bd.entryCount = e.size(); bd.entries = e.data();
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(ctx().device, &bd);
    wgpuBindGroupLayoutRelease(layout);
    groups.push_back(g);
    WGPUComputePassEncoder pe = wgpuCommandEncoderBeginComputePass(enc, nullptr);
    wgpuComputePassEncoderSetPipeline(pe, pipe);
    wgpuComputePassEncoderSetBindGroup(pe, 0, g, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pe, ps.gx, ps.gy, 1);
    wgpuComputePassEncoderEnd(pe);
    wgpuComputePassEncoderRelease(pe);
    return true;
}
bool mapStage(WGPUBuffer stage, uint64_t total, std::string& err){
    struct M { bool ok{false}; std::string msg; } mr;
    WGPUBufferMapCallbackInfo mcb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mcb.mode = WGPUCallbackMode_WaitAnyOnly; mcb.userdata1 = &mr;
    mcb.callback = [](WGPUMapAsyncStatus s, WGPUStringView m, void* u, void*){
        auto* r = (M*)u; r->ok = s == WGPUMapAsyncStatus_Success; if (!r->ok) r->msg = str(m); };
    waitFor(wgpuBufferMapAsync(stage, WGPUMapMode_Read, 0, (size_t)total, mcb));
    if (!mr.ok) err = "readback failed: " + mr.msg;
    return mr.ok;
}
// Copy the read-back channels into a FrameResult. f holds the nine channels in
// the order of the read list; in frame-only mode only f[7] is present.
void unpack(FrameResult& r, const float* const f[9], int w, int h, bool rtao, bool frameOnly){
    const uint32_t N = (uint32_t)w * (uint32_t)h;
    r = FrameResult();
    r.frame.resize(w, h);
    for (uint32_t i = 0; i < N; ++i) r.frame.px[i] = {f[7][4*i], f[7][4*i+1], f[7][4*i+2]};
    if (frameOnly) return;
    r.g.resize(w, h); r.aoSS.resize(w, h); r.hdr.resize(w, h);
    if (rtao) r.aoRT.resize(w, h);
    for (uint32_t i = 0; i < N; ++i){
        const bool cov = f[3][4*i+3] != 0.0f;
        r.g.depth.px[i]    = cov ? f[0][i] : std::numeric_limits<float>::infinity();
        r.g.position.px[i] = {f[1][4*i], f[1][4*i+1], f[1][4*i+2]};
        r.g.normal.px[i]   = {f[2][4*i], f[2][4*i+1], f[2][4*i+2]};
        r.g.albedo.px[i]   = {f[3][4*i], f[3][4*i+1], f[3][4*i+2]};
        r.g.mask.px[i]     = cov ? 1 : 0;
        r.g.motion.px[i]   = {f[4][4*i], f[4][4*i+1]};
        r.motionValid += f[4][4*i+3] != 0.0f; r.motionTotal += cov;
        r.aoSS.px[i] = f[5][i];
        if (rtao) r.aoRT.px[i] = f[6][i];
        r.hdr.px[i]  = {f[8][4*i], f[8][4*i+1], f[8][4*i+2]};
    }
}
// Encode every pass and the copies into the staging buffer, submit, and wait
// for the map. Returns false with the first WebGPU error.
bool runPasses(const std::vector<Pass>& passes, const std::vector<std::pair<WGPUBuffer, uint64_t>>& reads,
               WGPUBuffer stage, uint64_t total, std::string& err){
    Ctx& c = ctx();
    wgpuDevicePushErrorScope(c.device, WGPUErrorFilter_Validation);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(c.device, nullptr);
    std::vector<WGPUBindGroup> groups;
    bool ok = true;
    for (const Pass& ps : passes) if (ok) ok = encodePass(enc, ps, groups, err);
    uint64_t off = 0;
    for (const auto& rd : reads){ if (rd.second) wgpuCommandEncoderCopyBufferToBuffer(enc, rd.first, 0, stage, off, rd.second); off += rd.second; }
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    if (ok) wgpuQueueSubmit(c.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    for (WGPUBindGroup g : groups) wgpuBindGroupRelease(g);
    std::string scopeErr;
    if (!popScope("encode and submit", scopeErr) && ok){ err = scopeErr; ok = false; }
    return ok && mapStage(stage, total, err);
}
}  // namespace

bool renderGpu(const Scene& scene, int w, int h, const Mat4& prevVP,
               const RenderOptions& opts, FrameResult& r, std::string& err, bool frameOnly){
    GpuAdapterInfo info;
    if (!gpuInit(info, err)) return false;
    const std::vector<float> tris = packTriangles(scene);
    const uint32_t ntri = (uint32_t)(tris.size() / 24), N = (uint32_t)w * (uint32_t)h;
    const Mat4 vp = mul(scene.camera.proj(), scene.camera.view());
    float pf[48] = {};
    packParams(pf, scene, vp, prevVP, w, h, ntri, opts.rtao);
    const WGPUBufferUsage S = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    const uint64_t n4 = N * 4ull, n16 = N * 16ull, all = frameOnly ? 0 : 1;
    Frame b;
    b.P = buffer(sizeof(float) * 48, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    b.T = buffer(tris.size() * 4, S); b.SF = buffer(ntri * 64ull, S); b.SI = buffer(ntri * 16ull, S);
    b.depth = buffer(n4, S); b.pos = buffer(n16, S); b.nrm = buffer(n16, S); b.am = buffer(n16, S);
    b.mot = buffer(n16, S); b.ss = buffer(n4, S); b.rt = buffer(n4, S); b.frame = buffer(n16, S); b.hdr = buffer(n16, S);
    wgpuQueueWriteBuffer(ctx().queue, b.P, 0, pf, sizeof(float) * 48);
    wgpuQueueWriteBuffer(ctx().queue, b.T, 0, tris.data(), tris.size() * 4);
    const uint32_t gx = (uint32_t)(w + 7) / 8, gy = (uint32_t)(h + 7) / 8;
    std::vector<Pass> passes = {{"setup", {b.P, b.T, b.SF, b.SI}, (ntri + 63) / 64, 1},
        {"raster", {b.P, b.T, b.SF, b.SI, b.depth, b.pos, b.nrm, b.am}, gx, gy},
        {"motion", {b.P, b.pos, b.am, b.mot}, gx, gy}, {"ssao", {b.P, b.pos, b.nrm, b.am, b.ss}, gx, gy}};
    if (opts.rtao) passes.push_back({"rtao", {b.P, b.T, b.pos, b.nrm, b.am, b.rt}, gx, gy});
    passes.push_back({"shade", {b.P, b.nrm, b.am, opts.rtao ? b.rt : b.ss, b.frame, b.hdr}, gx, gy});
    // Read-back list in unpack() order; a size of zero is skipped.
    const std::vector<std::pair<WGPUBuffer, uint64_t>> reads = {{b.depth, n4*all}, {b.pos, n16*all}, {b.nrm, n16*all},
        {b.am, n16*all}, {b.mot, n16*all}, {b.ss, n4*all}, {b.rt, opts.rtao ? n4*all : 0}, {b.frame, n16}, {b.hdr, n16*all}};
    uint64_t total = 0; for (const auto& rd : reads) total += rd.second;
    b.stage = buffer(total, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    if (!runPasses(passes, reads, b.stage, total, err)) return false;
    const auto* bytes = (const unsigned char*)wgpuBufferGetConstMappedRange(b.stage, 0, (size_t)total);
    const float* f[9]; uint64_t off = 0;
    for (int i = 0; i < 9; ++i){ f[i] = (const float*)(bytes + off); off += reads[i].second; }
    unpack(r, f, w, h, opts.rtao, frameOnly);
    wgpuBufferUnmap(b.stage);
    if (frameOnly) return true;
    if (opts.rtao) r.rec = reconcile(r.aoSS, r.aoRT, r.g.mask, opts.tolerance);
    else r.rec.errorMap.resize(w, h);
    r.viewProj = vp;
    return true;
}
}
