// raw_native_cli held-time: see raw/tools/heldtime_cmd.hpp.
#include "raw/tools/heldtime_cmd.hpp"
#include "raw/tools/image_out.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace raw {
namespace {
struct Args { std::string out{"held-time"}, raw; int w{960}, h{540}, frames{96}, fps{24}, threads{3}, palette{64}; bool quick{false}; };
bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--out") && more) a.out = argv[++i];
        else if (!std::strcmp(argv[i], "--raw") && more) a.raw = argv[++i];
        else if (!std::strcmp(argv[i], "--w") && more) a.w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--h") && more) a.h = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && more) a.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--fps") && more) a.fps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--threads") && more) a.threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--palette") && more) a.palette = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--quick")) a.quick = true;
        else return false;
    }
    if (a.quick) { a.w = 160; a.h = 90; }
    return a.w > 0 && a.h > 0 && a.frames > 1 && a.fps > 0 && a.palette >= 2 && a.palette <= 255;
}
heldtime::DemoSpec spec(const anim::Rate& r, int w, int h, int frames, int fps) {
    heldtime::DemoSpec s; s.character = r; s.w = w; s.h = h; s.loop = {frames, {fps, 1}}; s.cameraPeriod = frames;
    return s;
}
struct Row { std::string name; int frames, fps, scale; LoopRender const* r; LoopFiles f; double seconds; };
void json(std::ofstream& j, const Row& x, bool last) {
    j << "  {\"loop\": \"" << x.name << "\", \"w\": " << x.r->w << ", \"h\": " << x.r->h << ", \"frames\": " << x.frames << ", \"fps\": " << x.fps
      << ", \"scale\": " << x.scale << ", \"closure_max_diff\": " << x.r->closureMaxDiff << ", \"palette\": " << x.f.paletteSize
      << ", \"off_palette\": " << x.f.offPalette << ", \"gif_exact\": " << (x.f.gifExact ? "true" : "false") << ", \"gif\": \"" << x.f.gif
      << "\", \"gif_bytes\": " << x.f.gifBytes << ", \"webp\": \"" << x.f.webp << "\", \"webp_bytes\": " << x.f.webpBytes << ", \"raw\": \"" << x.f.raw
      << "\", \"render_seconds\": " << x.seconds << "}" << (last ? "\n" : ",\n");
}
void stills(const LoopRender& r, const std::string& out, std::initializer_list<int> picks) {
    for (const int k : picks) if (k < int(r.frames.size())) writePng(out + "/" + r.name + "-f" + std::to_string(k) + ".png", r.w, r.h, r.frames[std::size_t(k)]);
}
}  // namespace

int heldTimeCommand(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) { std::printf("usage: raw_native_cli held-time [--out DIR] [--raw DIR] [--w N] [--h N] [--frames N] [--fps N] [--threads N] [--palette N] [--quick]\n"); return 2; }
    const anim::Rate rates[] = {anim::onN(1), anim::onN(2), anim::onN(3)};
    for (const anim::Rate& r : rates) {   // P2: refuse a loop that cannot close before rendering it
        const heldtime::DemoSpec s = spec(r, a.w, a.h, a.frames, a.fps);
        const anim::LoopCheck c = anim::checkLoop(s.loop, heldtime::demoTerms(s));
        if (!c.ok) { for (const std::string& why : c.refused) std::printf("held-time: loop refused: %s\n", why.c_str()); return 2; }
    }
    std::filesystem::create_directories(a.out);
    const char* names[] = {"ones", "twos", "threes"};
    std::vector<LoopRender> full, half;
    std::vector<Row> rows;
    const auto timed = [&](auto&& fn) { const auto t0 = std::chrono::steady_clock::now(); fn(); return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    full.reserve(3); half.reserve(4);
    for (int i = 0; i < 3; ++i) {
        const double sec = timed([&] { full.push_back(renderLoop(std::string("hatch-") + names[i], spec(rates[i], a.w, a.h, a.frames, a.fps), a.threads)); });
        stills(full.back(), a.out, {0, 1, 2, 3, 4, 5, a.frames / 2});
        rows.push_back({full.back().name, a.frames, a.fps, 1, nullptr, writeLoop(full.back(), a.out, a.raw, a.fps, 1, 1, a.palette), sec});
        std::printf("held-time: %s rendered in %.1f s, closure %d\n", full.back().name.c_str(), sec, full.back().closureMaxDiff);
        half.push_back(renderLoop(std::string("half-") + names[i], spec(rates[i], a.w / 2, a.h / 2, a.frames, a.fps), a.threads));
    }
    for (int i = 0; i < 3; ++i) rows[std::size_t(i)].r = &full[std::size_t(i)];
    const LoopRender tri = sideBySide("triptych-ones-twos-threes", {&half[0], &half[1], &half[2]});
    rows.push_back({tri.name, a.frames, a.fps, 1, &tri, writeLoop(tri, a.out, a.raw, a.fps, 1, 1, a.palette), 0});
    rows.push_back({tri.name, a.frames, a.fps, 2, &tri, writeLoop(tri, a.out, "", a.fps, 1, 2, a.palette), 0});
    stills(tri, a.out, {0, 1, 2, 3});
    if (!a.quick) {   // B3: 12 s at 12 fps, 480 x 270, twos (12 Hz), 32 colours
        double sec = 0;
        sec = timed([&] { half.push_back(renderLoop("size-12s-12fps-480x270", spec(anim::onN(2), 480, 270, 144, 12), a.threads)); });
        rows.push_back({half.back().name, 144, 12, 1, &half.back(), writeLoop(half.back(), a.out, a.raw, 12, 1, 1, 32), sec});
    }
    std::ofstream j(a.out + "/heldtime-loop-run.json");
    j << "[\n";
    for (std::size_t i = 0; i < rows.size(); ++i) json(j, rows[i], i + 1 == rows.size());
    j << "]\n";
    for (const Row& x : rows) std::printf("held-time: %s x%d closure %d palette %d off %d exact %d gif %lld B webp %lld B\n", x.name.c_str(), x.scale,
                                          x.r->closureMaxDiff, x.f.paletteSize, x.f.offPalette, int(x.f.gifExact), x.f.gifBytes, x.f.webpBytes);
    return 0;
}

}  // namespace raw
