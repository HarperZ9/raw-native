#pragma once
// Test helper: the C++ move table of every set of Low Water's moves.json expansion, keyed as the
// frame dump and the clip markers name moves ("<set> <id>": base, tool/<id>/<lamb|slow|foe>).
#include "raw/game/action/lw_moves.hpp"
#include "raw/game/action/markers.hpp"
#include <fstream>
#include <map>
#include <sstream>

namespace action_test {
using namespace raw::game::action;

inline const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/";
inline std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

struct Tables {
    std::vector<MoveTable> sets;
    std::map<std::string, std::pair<int, int>> where;   // key -> (set, move)
    const Move* find(const std::string& key) const {
        auto it = where.find(key);
        return it == where.end() ? nullptr : &sets[(size_t)it->second.first].at(it->second.second);
    }
};

inline void add_set(Tables& t, const std::string& name, const Json& defined) {
    t.sets.push_back(compile(defined));
    const int s = (int)t.sets.size() - 1;
    for (const Move& m : t.sets.back().moves) t.where[name + " " + m.id] = {s, t.sets.back().find(m.id)};
}

inline Tables load_tables() {
    const Json e = expand_all(load_lw_moves(slurp(DIR + "moves.json")));
    Tables t;
    add_set(t, "base", *e.get("base"));
    for (const auto& [tool, v] : e.get("tools")->o)
        for (const auto& [k, tab] : v.o) add_set(t, "tool/" + tool + "/" + k, tab);
    return t;
}

}  // namespace action_test
