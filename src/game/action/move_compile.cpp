// Defined lw-moves records into typed Moves: the windows become typed sections.
#include "raw/game/action/moves.hpp"
#include <cmath>
#include <stdexcept>

namespace raw::game::action {
namespace {

double n_or(const Json& o, const char* k, double d) { auto* p = o.get(k); return p && p->k == Json::Num ? p->n : d; }
bool b_or(const Json& o, const char* k, bool d) { auto* p = o.get(k); return p && p->k == Json::Bool ? p->b : d; }
const Json* window(const Json& o, const char* k) {
    auto* p = o.get(k);
    return p && p->k == Json::Arr && p->a.size() >= 2 ? p : nullptr;
}
Section sec(Sec k, double from, double to) { Section s; s.kind = k; s.from = (int)from; s.to = (int)to; return s; }

void scalars(Move& m, const Json& r) {
    m.kind = r.str_or("kind", "attack"); m.anim = r.str_or("anim", m.id);
    m.startup = (int)n_or(r, "startup", 0); m.active = (int)n_or(r, "active", 0); m.recovery = (int)n_or(r, "recovery", 0);
    m.total = m.startup + m.active + m.recovery;
    m.stamina = n_or(r, "stamina", 0); m.dmg = n_or(r, "dmg", 0); m.poise = n_or(r, "poise", 0);
    m.hitstop = n_or(r, "hitstop", 0); m.composure = n_or(r, "composure", 0);
    m.shake = n_or(r, "shake", 0);
    if (auto* w = r.get("sweep"); w && w->k == Json::Arr && w->a.size() >= 2) { m.smear_from = (int)w->a[0].n; m.smear_to = (int)w->a[1].n - 1; }
    m.parryable = b_or(r, "parryable", true); m.blockable = b_or(r, "blockable", true);
    m.knockdown = b_or(r, "knockdown", false); m.pierce = b_or(r, "pierce", false);
    auto* it = r.get("item"); m.item = it && it->k == Json::Obj;
}

void windows(Move& m, const Json& r, ActionClasses& cls) {
    if (auto* c = r.get("cancel"); c && c->k == Json::Obj)
        for (auto& [a, w] : c->o) {
            Section s = sec(Sec::Cancel, w.a[0].n, w.a[1].n);
            s.mask = a == "any" ? ActionClasses::all() : uint64_t(1) << cls.bit(a);
            m.sections.push_back(s);
        }
    if (auto* w = window(r, "iframes")) m.sections.push_back(sec(Sec::Invulnerable, w->a[0].n, w->a[1].n));
    if (auto* w = window(r, "parry")) m.sections.push_back(sec(Sec::Parry, w->a[0].n, w->a[1].n));
    if (auto* w = window(r, "armor")) { Section s = sec(Sec::Armor, w->a[0].n, w->a[1].n); s.value = w->a.size() > 2 ? w->a[2].n : 0; m.sections.push_back(s); }
    if (auto* t = r.get("tell"); t && t->k == Json::Obj) {
        if (auto* w = window(*t, "at")) m.sections.push_back(sec(Sec::Tell, w->a[0].n, w->a[1].n));
        m.tell_kind = t->str_or("kind");
    }
    if (auto* h = r.get("hits"); h && h->k == Json::Arr)
        for (auto& x : h->a) if (auto* w = window(x, "at")) {
            m.sections.push_back(sec(Sec::Hit, w->a[0].n, w->a[1].n));
            Ring g; g.on = (int)w->a[0].n; g.off = (int)w->a[1].n;
            if (auto* a = window(x, "arc")) { g.arc[0] = a->a[0].n; g.arc[1] = a->a[1].n; }
            if (auto* a = window(x, "r")) { g.r[0] = a->a[0].n; g.r[1] = a->a[1].n; }
            if (auto* a = window(x, "y")) { g.y[0] = a->a[0].n; g.y[1] = a->a[1].n; }
            m.rings.push_back(g);
        }
    // a press made from here on is held to the move (fighter.mjs heldPress): a dodge's from two
    // frames into its invulnerable window, anything else from the end of its wind-up
    int held = m.startup;
    if (m.kind == "dodge") { auto* w = window(r, "iframes"); held = w ? (int)w->a[0].n + 2 : 0; }
    m.sections.push_back(sec(Sec::Buffer, held, m.total - 1));
}

void links(Move& m, const Json& r, MoveTable& t) {
    auto id_of = [&](const Json* p) {
        if (!p || p->k != Json::Str) return -1;
        int i = t.find(p->s); if (i < 0) throw std::runtime_error("move " + m.id + ": unknown move " + p->s);
        return i;
    };
    m.then = id_of(r.get("then"));
    if (auto* nx = r.get("next"); nx && nx->k == Json::Obj)
        for (auto& [a, to] : nx->o) m.next.emplace_back(t.classes.bit(a), id_of(&to));
    if (auto* c = r.get("charge"); c && c->k == Json::Obj) {
        Section s = sec(Sec::Charge, n_or(*c, "at", 0), n_or(*c, "at", 0));
        s.value = n_or(*c, "max", 0); s.min = (int)n_or(*c, "min", 0); s.into = id_of(c->get("into"));
        m.sections.push_back(s);
    }
    if (auto* p = r.get("tap"); p && p->k == Json::Obj) {
        Section s = sec(Sec::TapRelease, 1, n_or(*p, "before", 0)); s.into = id_of(p->get("into"));
        m.sections.push_back(s);
    }
}

}  // namespace

MoveTable compile(const Json& defined) {
    MoveTable t;
    for (auto& [id, r] : defined.o) {
        Move m; m.id = id; scalars(m, r); windows(m, r, t.classes);
        t.index[id] = (int)t.moves.size(); t.moves.push_back(std::move(m));
    }
    for (auto& [id, r] : defined.o) links(t.moves[(size_t)t.index[id]], r, t);
    return t;
}

}  // namespace raw::game::action
