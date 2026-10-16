// HDR10 through the swapchain (N2 criterion 3, evidence/n2-presentation-bounds.json c3_hdr): the
// committed M1 grid through the C++ aces2-hdr1000/rec2100-pq pipeline, decoded to linear light,
// PQ-encoded by the present pass into an R10G10B10A2 backbuffer and read back before Present.
// Every channel must be within one 10-bit code of the C++ encode; the wrong-exponent control must not.
#include "present_host.hpp"
#include "raw/graph/frame_graph.hpp"
#include "raw/renderer/colour.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace raw::present {
namespace {
constexpr uint32_t kW = 256, kH = 144;
struct PqUniform { uint32_t width, count, pad0, pad1; };
struct Verdict { double maxAbs{0}; uint64_t over{0}, codesEqual{0}, codesOne{0}, channels{0}; uint32_t maxCodeDiff{0}; };

// The 10-bit codes of the read-back image against the C++ encode.
Verdict compare(const unsigned char* m, uint32_t pitch, const std::vector<colour::RGB>& cpu){
    Verdict v;
    for (size_t i = 0; i < cpu.size(); ++i){
        uint32_t word; std::memcpy(&word, m + size_t(i / kW) * pitch + (i % kW) * 4, 4);
        for (int c = 0; c < 3; ++c){
            const uint32_t code = (word >> (10 * c)) & 1023u;
            const double ref = cpu[i][c], d = std::fabs(code / 1023.0 - ref);
            const uint32_t refCode = (uint32_t)std::lround(std::fmin(std::fmax(ref, 0.0), 1.0) * 1023.0);
            const uint32_t cd = code > refCode ? code - refCode : refCode - code;
            v.maxAbs = std::fmax(v.maxAbs, d);
            v.maxCodeDiff = cd > v.maxCodeDiff ? cd : v.maxCodeDiff;
            v.over += d > 1.0 / 1023.0;
            v.codesEqual += cd == 0; v.codesOne += cd == 1; ++v.channels;
        }
    }
    return v;
}
bool drawPq(Session& s, rhi::RasterPipelineHandle pipe, rhi::BufferHandle lin, rhi::BufferHandle uni, rhi::BufferHandle rb,
            const std::vector<float>& linear, const std::vector<colour::RGB>& cpu, Verdict& v, std::string& err){
    using rhi::Access;
    const uint32_t pitch = rhi::textureRowPitch(kW, rhi::TextureFormat::RGB10A2Unorm);
    graph::FrameGraph g(s.dev.get());
    const graph::Resource L = g.importBuffer("linear", lin), R = g.importBuffer("readback", rb);
    const graph::Resource back = g.presentTo(*s.sc, 1, [&](std::string& e){
        const void* m = s.dev->mapRead(rb, uint64_t(pitch) * kH, e);
        if (!m) return false;
        v = compare((const unsigned char*)m, pitch, cpu);
        s.dev->unmap(rb);
        return true;
    });
    g.addPass("upload", {{L, Access::CopyDst}}, [&](graph::PassContext& c){
        c.cmd->upload(lin, 0, linear.data(), linear.size() * sizeof(float));
        const PqUniform u{kW, (uint32_t)cpu.size(), 0, 0};
        c.cmd->upload(uni, 0, &u, sizeof u);
    });
    g.addPass("pq", {{L, Access::StorageRead}, {back, Access::StorageWrite}}, [&](graph::PassContext& c){
        rhi::RenderPassDesc rp; rp.color[0].texture = s.sc->current();
        c.cmd->beginRenderPass(rp);
        const rhi::RasterBind b[] = {{uni, {}, {}}, {lin, {}, {}}};
        c.cmd->draw(pipe, b, 3);
        c.cmd->endRenderPass();
    });
    g.addPass("readback", {{back, Access::StorageRead}, {R, Access::CopyDst}}, [&](graph::PassContext& c){
        c.cmd->copyTextureToBuffer(s.sc->current(), rb);
    });
    g.markOutput(R);
    return g.execute(err);
}
std::string verdictJson(const Verdict& v){
    return "{\"max_abs\": " + jnum(v.maxAbs) + ", \"max_code_diff\": " + std::to_string(v.maxCodeDiff) + ", \"channels\": " + std::to_string(v.channels)
         + ", \"codes_equal\": " + std::to_string(v.codesEqual) + ", \"codes_one_apart\": " + std::to_string(v.codesOne)
         + ", \"over_bound\": " + std::to_string(v.over) + "}";
}
}  // namespace

int hdrCommand(const std::string& out, bool quiet){
    std::string err;
    colour::Pipeline pl;
    if (!colour::parsePipeline("aces2-hdr1000/rec2100-pq", pl)){ std::fprintf(stderr, "no aces2-hdr1000/rec2100-pq pipeline\n"); return kRuntime; }
    const colour::Transform tf(pl);
    const std::vector<colour::RGB> grid = colour::grid();
    std::vector<colour::RGB> cpu(grid.size());
    std::vector<float> linear(size_t(kW) * kH * 4, 0.0f);
    for (size_t i = 0; i < grid.size(); ++i){
        cpu[i] = tf.apply(grid[i]);
        for (int c = 0; c < 3; ++c) linear[i * 4 + c] = (float)colour::pqDecode(cpu[i][c]);
    }
    if (grid.size() > size_t(kW) * kH){ std::fprintf(stderr, "the grid does not fit %ux%u\n", kW, kH); return kRuntime; }
    platform::WindowDesc wd; wd.title = "raw-native present hdr"; wd.width = kW; wd.height = kH;
    auto win = platform::createWindow(wd, err);
    if (!win){ std::printf("SKIP: %s\n", err.c_str()); return kSkip; }
    Session s; bool noDevice = false;
    if (!openSession(s, win.get(), rhi::TextureFormat::RGB10A2Unorm, rhi::ColourSpace::Hdr10Pq, 1, false, err, &noDevice)){
        if (noDevice){ std::printf("SKIP: %s\n", err.c_str()); return kSkip; }
        // A surface without an HDR10 format (lavapipe, SwiftShader) is a skip with its reason, not a pass.
        if (err.find("offers no format") != std::string::npos){ std::printf("SKIP: HDR10 swapchain: %s\n", err.c_str()); return kSkip; }
        std::fprintf(stderr, "hdr: %s\n", err.c_str()); return kRuntime;
    }
    if (s.sc->width() != kW || s.sc->height() != kH){ std::fprintf(stderr, "hdr: the swapchain is %ux%u, not %ux%u\n", s.sc->width(), s.sc->height(), kW, kH); return kRuntime; }
    const rhi::BufferHandle lin = s.dev->createBuffer({linear.size() * sizeof(float), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, "hdr linear"}, err);
    const rhi::BufferHandle uni = s.dev->createBuffer({sizeof(PqUniform), rhi::BufferUsage::Uniform, "hdr uniform"}, err);
    const rhi::BufferHandle rb = s.dev->createBuffer({uint64_t(rhi::textureRowPitch(kW, rhi::TextureFormat::RGB10A2Unorm)) * kH,
                                                     rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst, "hdr readback"}, err);
    Verdict main, control;
    if (!lin.valid() || !uni.valid() || !rb.valid() || !drawPq(s, s.pipes.pq, lin, uni, rb, linear, cpu, main, err)
        || !drawPq(s, s.pipes.pqControl, lin, uni, rb, linear, cpu, control, err)){
        std::fprintf(stderr, "hdr: %s\n", err.c_str()); return kRuntime; }
    const bool applied = s.sc->colourSpaceApplied();
    const std::string adapter = s.adapter;
    for (rhi::BufferHandle b : {lin, uni, rb}) s.dev->destroyBuffer(b);
    closeSession(s);
    const bool pass = main.over == 0 && main.channels == grid.size() * 3 && control.over > 0;
    const std::string j = "{\n \"schema\": \"raw-native.present-hdr/1\",\n \"adapter\": " + jstr(adapter)
        + ",\n \"pipeline\": \"aces2-hdr1000/rec2100-pq\",\n \"inputs\": " + std::to_string(grid.size())
        + ",\n \"swapchain\": \"R10G10B10A2_UNORM 256x144, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020\",\n \"colour_space_applied\": " + (applied ? "true" : "false")
        + ",\n \"bound\": " + jnum(1.0 / 1023.0) + ",\n \"presented\": " + verdictJson(main) + ",\n \"control_m1_2610_over_4096\": " + verdictJson(control)
        + ",\n \"pass\": " + (pass ? "true" : "false") + "\n}\n";
    if (!out.empty() && !writeText(out, j)){ std::fprintf(stderr, "cannot write %s\n", out.c_str()); return kRuntime; }
    if (!quiet) std::printf("hdr: max %.3g (%u codes), over %llu; control max %.3g, over %llu; colour space %s -> %s\n", main.maxAbs, main.maxCodeDiff,
                            (unsigned long long)main.over, control.maxAbs, (unsigned long long)control.over, applied ? "applied" : "not applied", pass ? "pass" : "FAIL");
    return pass ? kOk : kCheckFailed;
}
}  // namespace raw::present
