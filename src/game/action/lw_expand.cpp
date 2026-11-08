// The whole lw-moves expansion in the shape of Low Water's moves-expanded.golden.json
// (combat/tools/export-data.mjs expandAll), and the frame-data lines of tools/action/dump_frames.mjs.
#include "raw/game/action/lw_moves.hpp"
#include <algorithm>

namespace raw::game::action {
namespace {

using raw::story::dump_number;

void merge(Json& into, const Json& from) { for (auto& [k, v] : from.o) put(into, k, v); }

std::vector<std::string> sorted_keys(const Json& o) {
    std::vector<std::string> k; for (auto& p : o.o) k.push_back(p.first);
    std::sort(k.begin(), k.end()); return k;
}

}  // namespace

Json expand_all(const LwMoves& lw, double foe_windup) {
    Json t = Json::obj();
    merge(t, lw.common); merge(t, lw.body);
    merge(t, lw.arena_specials); merge(t, lw.foe_specials);   // SPECIALS: the arena specials, then the foes'
    merge(t, lw.boss_specials); merge(t, lw.arena_specials);
    for (auto& cls : sorted_keys(lw.tool_classes)) {
        Json p = *lw.tool_classes.get(cls); put(p, "cls", Json::str(cls)); put(p, "id", Json::str(cls));
        ToolGen g; g.prefix = cls + ":";
        merge(t, build_tool_moves(p, g));
    }
    Json out = Json::obj();
    put(out, "base", sort_keys(define_moves(t)));
    Json tools = Json::obj();
    for (auto& id : sorted_keys(lw.tools)) {
        Json P = tool_profile(lw, id), v = Json::obj();
        ToolGen lamb, slow, foe; slow.slow = true; foe.windup = foe_windup;
        put(v, "lamb", define_moves(build_tool_moves(P, lamb)));
        put(v, "slow", define_moves(build_tool_moves(P, slow)));
        put(v, "foe", define_moves(build_tool_moves(P, foe)));
        put(tools, id, sort_keys(v));
    }
    put(out, "tools", tools);
    return out;
}

std::string frame_line(const std::string& set, const Json& m) {
    auto n = [&](const char* k) { auto* p = m.get(k); return dump_number(p ? p->n : 0); };
    std::string c;
    const Json* cancel = m.get("cancel");
    if (cancel) for (auto& k : sorted_keys(*cancel)) {
        const Json& w = *cancel->get(k);
        if (!c.empty()) c += ",";
        c += k + ":" + dump_number(w.a[0].n) + "-" + dump_number(w.a[1].n);
    }
    return set + " " + m.str_or("id") + " startup=" + n("startup") + " active=" + n("active") + " recovery=" + n("recovery") +
           " free=" + n("total") + " cancel=" + (c.empty() ? "-" : c);
}

std::vector<std::string> frame_lines(const Json& e) {
    std::vector<std::string> out;
    if (auto* b = e.get("base")) for (auto& [id, m] : b->o) out.push_back(frame_line("base", m));
    if (auto* t = e.get("tools"))
        for (auto& [tool, v] : t->o) for (auto& [k, tab] : v.o) for (auto& [id, m] : tab.o) out.push_back(frame_line("tool/" + tool + "/" + k, m));
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace raw::game::action
