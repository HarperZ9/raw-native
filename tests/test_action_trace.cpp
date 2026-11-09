// raw/game/action trace parity: the 40 action-timeline traces recorded from Low Water's JS engine
// (tests/data/action/traces, tools/action/record_timeline.mjs) replay byte-identical in C++.
// Planted: one byte flipped in a recorded trace is caught; a buffer of 7 frames in place of 8
// breaks at least one trace. Also counts, from the recorded traces, presses made while free.
#include "check.hpp"
#include "raw/core/sha256.hpp"
#include "raw/game/action/trace.hpp"
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace raw::game::action;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/traces/";

// A press is judged when the trace shows its actor free, not held, with an empty buffer at the
// end of the tick before. It must start a move on its own tick, or be refused for breath, or be a
// parry press inside the lockout after a parry move that started in the last 60 ticks (T5).
struct Zero { int judged = 0, started = 0, breathless = 0, lockout = 0, other = 0; };
static void zero_frames(const std::string& text, Zero& z) {
    std::istringstream in(text); std::string line;
    std::map<std::string, Json> last;  // actor -> its state row
    std::map<int, std::set<std::string>> started, refused;
    std::map<std::string, int> last_catch;   // actor -> tick its last parry move started
    std::vector<std::pair<int, std::string>> presses;
    int last_tick = 0;
    while (std::getline(in, line)) {
        Json r = raw::story::parse_json(line); const std::string t = r.str_or("t"); const int k = r.get("tick") ? (int)r.get("tick")->n : 0;
        if (t == "ast") { for (auto& row : r.get("a")->a) last[row.a[0].s] = row; last_tick = k; }
        if (t == "aev" && r.str_or("n") == "move") started[k].insert(r.get("d")->str_or("id"));
        if (t == "aev" && r.str_or("n") == "move" && r.get("d")->str_or("move").ends_with("catch")) last_catch[r.get("d")->str_or("id")] = k;
        if (t == "aev" && r.str_or("n") == "breathless") refused[k].insert(r.get("d")->str_or("id"));
        if (t == "ain" && !r.get("press")->a.empty() && last_tick == k - 1 && last.count(r.str_or("id"))) {
            const Json& s = last[r.str_or("id")];
            const bool locked = r.get("press")->a[0].s == "catch" && last_catch.count(r.str_or("id")) && k - last_catch[r.str_or("id")] <= 60;
            if (s.a[1].k == Json::Null && s.a[6].n == 0 && s.a[7].n == 0) presses.emplace_back(k, r.str_or("id") + (locked ? "#locked" : ""));
        }
    }
    for (auto& [k, key] : presses) {
        const std::string id = key.substr(0, key.find('#'));
        z.judged++;
        if (started[k].count(id)) z.started++; else if (refused[k].count(id)) z.breathless++;
        else if (key.ends_with("#locked")) z.lockout++; else z.other++;
    }
}

int main() {
    const TraceTables tables = load_trace_tables(slurp(DIR + "tables.json"));
    const Json manifest = raw::story::parse_json(slurp(DIR + "manifest.json"));
    int same = 0, hashed = 0, broke7 = 0; Zero z;
    std::string first;
    for (auto& [name, info] : manifest.o) {
        const std::string rec = slurp(DIR + name);
        hashed += raw::sha256Hex(rec) == info.str_or("sha256");
        const TraceResult r = replay_trace(rec, tables);
        if (!r.error.empty()) std::printf("%s: %s\n", name.c_str(), r.error.c_str());
        const bool ok = r.error.empty() && r.trace == rec;
        if (!ok) std::printf("%s differs at line %zu\n", name.c_str(), first_line_difference(r.trace, rec));
        same += ok;
        broke7 += replay_trace(rec, tables, [](Config& c) { c.buffer = 7; }).trace != rec;
        zero_frames(rec, z);
        if (first.empty()) first = name;
    }
    std::printf("traces: %d of %zu byte-identical, %d of %zu match the manifest hash\n", same, manifest.o.size(), hashed, manifest.o.size());
    std::printf("planted buffer 7: %d of %zu traces differ\n", broke7, manifest.o.size());
    std::printf("presses made while free: %d judged, %d started on the press tick, %d refused for breath, %d in the parry lockout, %d other\n",
                z.judged, z.started, z.breathless, z.lockout, z.other);
    CHECK(manifest.o.size() == 40 && same == 40 && hashed == 40);
    CHECK(broke7 > 0);
    CHECK(z.judged > 0 && z.other == 0);
    // planted: one byte flipped in an ast line of the first trace
    std::string rec = slurp(DIR + first), flipped = rec;
    const size_t at = flipped.find("\"t\":\"ast\"", flipped.size() / 2);
    flipped[flipped.find(',', at + 20) - 1] ^= 1;
    const TraceResult r = replay_trace(flipped, tables);
    CHECK(r.trace != flipped && first_line_difference(r.trace, flipped) > 0);
    return raw_test_summary();
}
