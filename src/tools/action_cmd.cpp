// raw_native_cli action-dump (raw/tools/action_cmd.hpp).
#include "raw/tools/action_cmd.hpp"
#include "raw/game/action/lw_moves.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace raw {
namespace {

using namespace raw::game::action;

bool slurp(const char* path, std::string& out) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::stringstream s; s << f.rdbuf(); out = s.str(); return true;
}

Json expanded(const char* path, int argc, char** argv) {
    std::string text;
    if (!slurp(path, text)) throw std::runtime_error(std::string("cannot read ") + path);
    double windup = 1.6;
    for (int i = 0; i + 1 < argc; i++) if (std::strcmp(argv[i], "--foe-windup") == 0) windup = std::atof(argv[i + 1]);
    return expand_all(load_lw_moves(text), windup);
}

// The defined table a set names: base, or tool/<tool id>/<lamb|slow|foe>.
const Json* find_set(const Json& e, const std::string& set) {
    if (set == "base") return e.get("base");
    if (set.rfind("tool/", 0) != 0) return nullptr;
    const size_t slash = set.find('/', 5);
    if (slash == std::string::npos || !e.get("tools")) return nullptr;
    const Json* tool = e.get("tools")->get(set.substr(5, slash - 5));
    return tool ? tool->get(set.substr(slash + 1)) : nullptr;
}

// One row a section: '#' on the frames it covers, '.' elsewhere.
void strip_row(const char* label, int total, const std::vector<std::pair<int, int>>& spans) {
    std::string row((size_t)total, '.');
    for (auto [a, b] : spans) for (int f = a; f <= b && f < total; f++) if (f >= 0) row[(size_t)f] = '#';
    std::printf("%-14s %s\n", label, row.c_str());
}

void print_strip(const std::string& set, const Json& tab, const std::string& id) {
    MoveTable t = compile(tab);
    const Json& rec = *tab.get(id);
    const Move& m = t.at(t.find(id));
    std::printf("%s %s\n", set.c_str(), frame_line(set, rec).substr(set.size() + 1).c_str());
    std::string ruler, phase;
    for (int f = 0; f < m.total; f++) {
        ruler += f % 10 == 0 ? (char)('0' + (f / 10) % 10) : ' ';
        phase += f < m.startup ? 'S' : f < m.startup + m.active ? 'A' : 'R';
    }
    std::printf("%-14s %s|free %d\n%-14s %s\n", "frame", ruler.c_str(), m.total, "phase", phase.c_str());
    for (const auto& s : m.sections) {
        if (s.kind == Sec::Cancel) {
            std::string who;
            for (size_t b = 0; b < t.classes.names.size(); b++) if (s.mask >> b & 1) who += (who.empty() ? "" : ",") + t.classes.names[b];
            if (s.mask == ActionClasses::all()) who = "any";
            strip_row(("cancel " + who).substr(0, 14).c_str(), m.total, {{s.from, s.to}});
        } else {
            strip_row(sec_name(s.kind), m.total, {{s.from, s.to}});
        }
    }
}

std::map<std::string, std::string> table_lines(const std::string& text) {
    std::map<std::string, std::string> out; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
        out[line.substr(0, b)] = line;
    }
    return out;
}

int diff(const char* pa, const char* pb) {
    std::string ta, tb;
    if (!slurp(pa, ta) || !slurp(pb, tb)) { std::fprintf(stderr, "action-dump: cannot read the tables\n"); return 2; }
    auto A = table_lines(ta), B = table_lines(tb);
    int n = 0;
    for (auto& [k, v] : A) {
        auto it = B.find(k);
        if (it == B.end()) { std::printf("only in a: %s\n", v.c_str()); n++; }
        else if (it->second != v) { std::printf("a: %s\nb: %s\n", v.c_str(), it->second.c_str()); n++; }
    }
    for (auto& [k, v] : B) if (!A.count(k)) { std::printf("only in b: %s\n", v.c_str()); n++; }
    std::printf("%zu moves in a, %zu in b, %d differ\n", A.size(), B.size(), n);
    return n ? 1 : 0;
}

}  // namespace

int actionDumpCommand(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: action-dump table <moves.json> | strip <moves.json> <set> <id> | diff <a> <b>\n"); return 2; }
    const std::string sub = argv[1];
    try {
        if (sub == "diff" && argc >= 4) return diff(argv[2], argv[3]);
        if (sub == "table") { for (auto& l : frame_lines(expanded(argv[2], argc, argv))) std::printf("%s\n", l.c_str()); return 0; }
        if (sub == "strip" && argc >= 5) {
            Json e = expanded(argv[2], argc, argv);
            const Json* tab = find_set(e, argv[3]);
            if (!tab || !tab->get(argv[4])) { std::fprintf(stderr, "action-dump: no move %s in %s\n", argv[4], argv[3]); return 2; }
            print_strip(argv[3], *tab, argv[4]); return 0;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "action-dump: %s\n", ex.what()); return 2;
    }
    std::fprintf(stderr, "action-dump: unknown subcommand %s\n", sub.c_str());
    return 2;
}

}  // namespace raw
