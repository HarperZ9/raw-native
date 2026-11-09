// The impact preview bench: the sound track and the command. See raw/tools/impact_cmd.hpp.
#include "raw/tools/impact_cmd.hpp"
#include "raw/core/loop_media.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace raw {
namespace {

using game::action::Channel;
constexpr double PI = 3.14159265358979323846;

// One voice: a low body for a blow, a ring for a catch, a breath of noise for a slipped blow.
void voice(std::vector<float>& out, long at, const std::string& kind, double gain, int rate, unsigned seed) {
    const double f0 = kind == "hit.heavy" ? 70 : kind == "hit.light" ? 160 : 900, tb = kind == "hit.heavy" ? 0.09 : kind == "hit.light" ? 0.05 : 0.12;
    const double tn = kind == "catch" ? 0.01 : kind == "slip" ? 0.03 : kind == "hit.heavy" ? 0.025 : 0.015, nl = kind == "slip" ? 0.3 : kind == "catch" ? 0.3 : 0.45;
    const long n = (long)(rate * (kind == "catch" ? 0.5 : 0.35));
    unsigned s = seed * 2654435761u + 1;
    for (long i = 0; i < n && at + i < (long)out.size(); i++) {
        if (at + i < 0) continue;
        const double t = (double)i / rate;
        s = s * 1664525u + 1013904223u;
        const double noise = ((s >> 8) / 8388608.0 - 1) * nl * std::exp(-t / tn);
        double tone = kind == "slip" ? 0 : std::sin(2 * PI * f0 * t) * std::exp(-t / tb);
        if (kind == "catch") tone += 0.6 * std::sin(2 * PI * 1350 * t) * std::exp(-t / tb);
        out[(size_t)(at + i)] += (float)(gain * 0.7 * (tone + noise));
    }
}

std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

std::vector<std::uint8_t> shrink(const std::vector<std::uint8_t>& rgb, int w, int h, int k) {
    const int W = w / k, H = h / k;
    std::vector<std::uint8_t> o((size_t)W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) for (int c = 0; c < 3; c++) {
        int sum = 0; for (int j = 0; j < k; j++) for (int i = 0; i < k; i++) sum += rgb[((size_t)(y * k + j) * w + (x * k + i)) * 3 + c];
        o[((size_t)y * W + x) * 3 + c] = (std::uint8_t)((sum + k * k / 2) / (k * k));
    }
    return o;
}

}  // namespace

std::vector<float> impact_audio(const game::action::Schedule& s, int from, int frames, int rate, int contact) {
    std::vector<float> out((size_t)((long)frames * rate / 60), 0.0f);
    for (const auto& c : s.cues)
        if (c.ch == Channel::Sound && (contact < 0 || c.contact == contact))
            voice(out, std::lround((double)(c.tick - from) * rate / 60), c.kind, c.v[0], rate, (unsigned)(c.tick * 131 + c.contact));
    for (float& x : out) x = std::clamp(x, -1.0f, 1.0f);
    return out;
}

long first_onset(const std::vector<float>& samples, long start, double level) {
    for (long i = std::max(0L, start); i < (long)samples.size(); i++) if (std::fabs(samples[(size_t)i]) > level) return i;
    return -1;
}

bool write_wav(const std::string& path, const std::vector<float>& x, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](std::uint32_t v) { f.put((char)(v & 255)); f.put((char)(v >> 8 & 255)); f.put((char)(v >> 16 & 255)); f.put((char)(v >> 24)); };
    auto u16 = [&](std::uint16_t v) { f.put((char)(v & 255)); f.put((char)(v >> 8)); };
    const std::uint32_t data = (std::uint32_t)x.size() * 2;
    f.write("RIFF", 4); u32(36 + data); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32((std::uint32_t)rate); u32((std::uint32_t)rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(data);
    for (float v : x) u16((std::uint16_t)(std::int16_t)std::lround(v * 32767));
    return (bool)f;
}

int actionImpactCommand(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: raw_native_cli action-impact <trace.jsonl> <tables.json> <outdir> [--from T] [--frames N] [--w W] [--h H] [--contact I] [--around N] [--loop W] [--no-overlays] [--sound-late N]\n"); return 2; }
    ImpactView v; game::action::ScheduleOpts so; int contact = -1, around = 12, loop = 0;
    for (int i = 4; i < argc; i++) {
        auto n = [&](int& x) { if (i + 1 < argc) x = std::atoi(argv[++i]); };
        if (!std::strcmp(argv[i], "--from")) n(v.from); else if (!std::strcmp(argv[i], "--frames")) n(v.frames);
        else if (!std::strcmp(argv[i], "--w")) n(v.w); else if (!std::strcmp(argv[i], "--h")) n(v.h);
        else if (!std::strcmp(argv[i], "--contact")) n(contact); else if (!std::strcmp(argv[i], "--around")) n(around);
        else if (!std::strcmp(argv[i], "--loop")) n(loop); else if (!std::strcmp(argv[i], "--sound-late")) n(so.sound_late);
        else if (!std::strcmp(argv[i], "--no-overlays")) v.overlays = false;
        else if (!std::strcmp(argv[i], "--metres") && i + 1 < argc) v.metres = std::atof(argv[++i]);
        else { std::printf("action-impact: unknown option %s\n", argv[i]); return 2; }
    }
    std::string err;
    const game::action::Schedule s = game::action::schedule_trace(slurp(argv[1]), game::action::load_trace_tables(slurp(argv[2])), so, &err);
    if (!err.empty()) { std::printf("action-impact: %s\n", err.c_str()); return 2; }
    if (contact >= 0) { if (contact >= (int)s.contacts.size()) { std::printf("action-impact: %zu contacts\n", s.contacts.size()); return 2; } v.from = std::max(1, s.contacts[(size_t)contact].tick - around); }
    const std::string out = argv[3];
    std::filesystem::create_directories(out);
    const ImpactFrames fr = render_impact(s, v);
    for (int i = 0; i < v.frames; i++) {
        char name[64]; std::snprintf(name, sizeof name, "/frame-%04d.png", v.from + i);
        if (!writePng(out + name, v.w, v.h, fr.rgb[(size_t)i])) { std::printf("action-impact: cannot write %s\n", name); return 2; }
    }
    const std::vector<float> wav = impact_audio(s, v.from, v.frames);
    write_wav(out + "/contact.wav", wav, 48000);
    std::ofstream j(out + "/run.json");
    j << "{\"from\":" << v.from << ",\"frames\":" << v.frames << ",\"w\":" << v.w << ",\"h\":" << v.h << ",\"contacts\":[";
    bool first = true;
    for (size_t c = 0; c < s.contacts.size(); c++) {
        const auto& r = s.contacts[c];
        if (r.tick < v.from || r.tick >= v.from + v.frames) continue;
        const long on = first_onset(impact_audio(s, v.from, v.frames, 48000, (int)c), 0, 0.02);
        const double ms = on < 0 || fr.burst_first[c] < 0 ? -1 : on * 1000.0 / 48000 - fr.burst_first[c] * 1000.0 / 60;
        j << (first ? "" : ",") << "{\"i\":" << c << ",\"tick\":" << r.tick << ",\"move\":\"" << r.move << "\",\"result\":\"" << r.result
          << "\",\"burst_frame\":" << fr.burst_first[c] << ",\"sound_minus_frame_ms\":" << ms << "}";
        first = false;
    }
    double peak = 0; for (double p : fr.shake_px) peak = std::max(peak, p);
    j << "],\"shake_peak_px\":" << peak << "}\n";
    if (loop > 0) {
        const int k = std::max(1, v.w / loop), W = v.w / k, H = v.h / k;
        std::vector<media::Frame> small;
        for (auto& f : fr.rgb) small.push_back(shrink(f, v.w, v.h, k));
        const auto pal = media::medianCut(small, 64);
        std::vector<std::vector<std::uint8_t>> idx; for (auto& f : small) idx.push_back(media::mapNearest(f, pal));
        media::GifOptions go; go.w = W; go.h = H; go.delaysCs = media::frameDelays((int)small.size(), 60, 1, 100);
        media::WebpOptions wo; wo.w = W; wo.h = H; wo.delaysMs = media::frameDelays((int)small.size(), 60, 1, 1000);
        if (!media::writeFile(out + "/loop.gif", media::encodeGif(pal, idx, go)) || !media::writeFile(out + "/loop.webp", media::encodeWebp(small, wo))) return 2;
    }
    std::printf("action-impact: %d frames from tick %d, %zu contacts in the trace, shake peak %.2f px\n", v.frames, v.from, s.contacts.size(), peak);
    return 0;
}

}  // namespace raw
