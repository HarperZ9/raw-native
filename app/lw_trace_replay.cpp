// lw_trace_replay: replays golden story traces (recorded by Low Water's JS harness) through the C++ story VM
// and checks the output is byte-identical.
//   lw_trace_replay --act <root.lwd> [--out dir] [--compute checks,sheet,meters] [--class policy=archetype]... <golden.jsonl>...
// A golden whose file name starts with "e1-" is the era 1 cut (until "e1."). Exit 0 only when all pass.
// --compute answers those host modules in C++ instead of reading the recorded answers (the recorded answer is
// then only compared). --class names the sheet a begin record's policy starts from.
#include "raw/game/lowwater/replay.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("cannot read " + p); std::stringstream s; s << f.rdbuf(); return s.str(); }

static bool parse_compute(std::string list, raw::game::lowwater::Compute& cfg) {
    list += ',';
    for (size_t p; (p = list.find(',')) != std::string::npos; list.erase(0, p + 1)) {
        std::string x = list.substr(0, p);
        if (x == "checks") cfg.checks = true; else if (x == "sheet") cfg.sheet = true; else if (x == "meters") cfg.meters = true;
        else if (!x.empty()) { std::fprintf(stderr, "unknown module %s (checks, sheet, meters)\n", x.c_str()); return false; }
    }
    return true;
}

int main(int argc, char** argv) {
    std::string act, out; std::vector<std::string> files; raw::game::lowwater::Compute cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--act" && i + 1 < argc) act = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--compute" && i + 1 < argc) { if (!parse_compute(argv[++i], cfg)) return 2; }
        else if (a == "--class" && i + 1 < argc) {
            std::string c = argv[++i]; size_t e = c.find('=');
            if (e == std::string::npos) { std::fprintf(stderr, "--class policy=archetype\n"); return 2; }
            cfg.class_sheets[c.substr(0, e)] = c.substr(e + 1);
        }
        else files.push_back(a);
    }
    if (act.empty() || files.empty()) { std::fprintf(stderr, "usage: lw_trace_replay --act root.lwd [--out dir] [--compute m,..] [--class p=a] golden.jsonl...\n"); return 2; }
    using namespace std::chrono;
    std::vector<raw::story::Source> sources; double parse_ms = 0;
    try { auto t0 = steady_clock::now(); sources = raw::story::load_act_sources(act, [](const std::string& p) { return slurp(p); }); raw::story::load_book(sources); parse_ms = duration<double, std::milli>(steady_clock::now() - t0).count(); }
    catch (std::exception& e) { std::fprintf(stderr, "act: %s\n", e.what()); return 2; }
    std::map<std::string, raw::game::lowwater::ModuleStat> agg;
    size_t pass = 0; double total_ms = 0; size_t records = 0;
    for (auto& f : files) {
        std::string name = f.substr(f.find_last_of("/\\") + 1), golden = slurp(f);
        auto t0 = steady_clock::now();
        auto r = raw::game::lowwater::replay(golden, sources, name.rfind("e1-", 0) == 0 ? "e1." : "", &cfg);
        total_ms += duration<double, std::milli>(steady_clock::now() - t0).count(); records += r.vm_records;
        size_t d = raw::game::lowwater::first_difference(r.trace, golden);
        bool ok = d == 0 && r.error.empty(); pass += ok;
        std::printf("%s %s", ok ? "PASS" : "FAIL", name.c_str());
        if (!ok) std::printf("  first difference at line %zu%s%s", d, r.error.empty() ? "" : "  ", r.error.c_str());
        std::printf("\n");
        for (auto& [m, st] : r.modules) {
            agg[m].computed += st.computed; agg[m].mismatched += st.mismatched;
            if (agg[m].first.empty() && !st.first.empty()) agg[m].first = name + ": " + st.first;
        }
        if (!out.empty()) { std::ofstream o(out + "/" + name, std::ios::binary); o << r.trace; }
    }
    for (auto& [m, st] : agg) {
        std::printf("module %s: %zu answers computed, %zu differ from the recorded answer", m.c_str(), st.computed, st.mismatched);
        if (!st.first.empty()) std::printf("; first: %s", st.first.c_str());
        std::printf("\n");
    }
    std::printf("%zu of %zu traces byte-identical; parse %.1f ms; replay %.1f ms total, %.2f us per record (%zu records)\n", pass, files.size(), parse_ms, total_ms, records ? total_ms * 1000 / records : 0.0, records);
    return pass == files.size() ? 0 : 1;
}
