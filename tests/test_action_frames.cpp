// raw/game/action frame data against Low Water's JS engine (tests/data/action/SOURCE.md):
//  - the C++ expansion of moves.json, serialised, is byte-identical to the JS golden
//    (moves-expanded.golden.json, from combat/tools/export-data.mjs);
//  - every move's frame line (startup, active, recovery, free, cancel windows) equals the JS
//    dump (frames.txt, from tools/action/dump_frames.mjs);
//  - planted: one startup shifted by one frame in a copy of the dump is reported, and only it;
//    one byte changed in a copy of the golden fails the byte comparison.
#include "check.hpp"
#include "raw/game/action/lw_moves.hpp"
#include <fstream>
#include <map>
#include <sstream>

using namespace raw::game::action;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/";

static std::map<std::string, std::string> keyed(const std::vector<std::string>& lines) {
    std::map<std::string, std::string> m;
    for (auto& l : lines) { const size_t a = l.find(' '), b = l.find(' ', a + 1); m[l.substr(0, b)] = l; }
    return m;
}
static std::vector<std::string> split(const std::string& t) {
    std::vector<std::string> v; std::istringstream in(t); std::string l;
    while (std::getline(in, l)) if (!l.empty()) v.push_back(l);
    return v;
}
// Moves whose lines differ between two keyed tables, or that only one has.
static std::vector<std::string> mismatches(const std::map<std::string, std::string>& a, const std::map<std::string, std::string>& b) {
    std::vector<std::string> out;
    for (auto& [k, v] : a) { auto it = b.find(k); if (it == b.end() || it->second != v) out.push_back(k); }
    for (auto& [k, v] : b) if (!a.count(k)) out.push_back(k);
    return out;
}

int main() {
    const LwMoves lw = load_lw_moves(slurp(DIR + "moves.json"));
    const Json e = expand_all(lw);
    // A1b: the whole expansion, byte for byte
    const std::string golden = slurp(DIR + "moves-expanded.golden.json");
    const std::string mine = raw::story::dump_json(e) + "\n";
    CHECK(mine == golden);
    std::printf("expansion: %zu bytes, golden %zu bytes, %s\n", mine.size(), golden.size(), mine == golden ? "identical" : "DIFFERENT");
    std::string planted_golden = golden; planted_golden[golden.find("\"startup\":") + 10] ^= 1;
    CHECK(mine != planted_golden);
    // A1: the frame lines
    CHECK(e.get("base")->o.size() == 104); CHECK(e.get("tools")->o.size() == 8);
    const auto js = keyed(split(slurp(DIR + "frames.txt"))), cpp = keyed(frame_lines(e));
    const auto bad = mismatches(cpp, js);
    std::printf("frame data: %zu moves in C++, %zu in the JS dump, %zu mismatches\n", cpp.size(), js.size(), bad.size());
    for (auto& k : bad) std::printf("  mismatch: %s\n", k.c_str());
    CHECK(cpp.size() == 230 && js.size() == cpp.size() && bad.empty());
    // planted: base light startup +1 in a copy of the JS dump
    auto shifted = js; std::string& l = shifted.at("base light:heavy");
    const size_t p = l.find("startup=") + 8, q = l.find(' ', p);
    l.replace(p, q - p, std::to_string(std::stoi(l.substr(p, q - p)) + 1));
    const auto caught = mismatches(cpp, shifted);
    CHECK(caught.size() == 1 && caught[0] == "base light:heavy");
    std::printf("planted startup +1 on base light:heavy: %zu reported\n", caught.size());
    return raw_test_summary();
}
