// raw_native_cli action-dump: the C++ table of moves.json diffs clean against the JS dump, a
// table with one startup shifted by a frame diffs as one move, a strip prints, a missing move is
// bad input.
#include "check.hpp"
#include "raw/tools/action_cmd.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/";

static int run(std::vector<std::string> args) {
    args.insert(args.begin(), "action-dump");
    std::vector<char*> argv; for (auto& a : args) argv.push_back(a.data());
    return raw::actionDumpCommand((int)argv.size(), argv.data());
}

int main() {
    const std::string js = DIR + "frames.txt", planted = "action_dump_planted.txt";
    CHECK(run({"diff", js, js}) == 0);
    std::string t = slurp(js); const size_t at = t.find("base light:heavy startup=21 ");
    CHECK(at != std::string::npos);
    t.replace(at, 28, "base light:heavy startup=22 ");
    { std::ofstream o(planted, std::ios::binary); o << t; }
    CHECK(run({"diff", js, planted}) == 1);
    std::remove(planted.c_str());
    CHECK(run({"strip", DIR + "moves.json", "tool/w.boathook/foe", "light_1"}) == 0);
    CHECK(run({"strip", DIR + "moves.json", "base", "no-such-move"}) == 2);
    CHECK(run({"table", DIR + "missing.json"}) == 2);
    return raw_test_summary();
}
