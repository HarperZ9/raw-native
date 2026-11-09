// Sheet and checks: the host answers of the story VM, computed
// (rules/sheet.mjs, checks.mjs, creation.mjs, inventory.mjs in Low Water). Meters and the dispatch of
// host calls are in lowwater_host.cpp.
#include "raw/game/lowwater/rules.hpp"
#include <algorithm>
#include <cmath>

namespace raw::game::lowwater {
using namespace raw::story;

namespace {
double js_num(const Val& v) { double x = to_num(v); return std::isnan(x) ? 0 : x; }   // JS: +x || 0
const char* DEFAULT_LABELS[][2] = {{"0", "Trivial"}, {"8", "Easy"}, {"10", "Medium"}, {"12", "Challenging"}, {"14", "Formidable"}, {"16", "Legendary"}, {"18", "Heroic"}, {"20", "Impossible"}};
std::string val_str(const Val& v) { return v.k == Val::S ? v.s : v.k == Val::N ? dump_number(v.n) : v.k == Val::B ? (v.b ? "true" : "false") : "undefined"; }
double dnum(const Json* j, double d = 0) { return j && j->k == Json::Num ? j->n : d; }
std::string dstr(const Json* j) { return j && j->k == Json::Str ? j->s : ""; }
const Json* at(const Json& a, size_t i) { return i < a.a.size() ? &a.a[i] : nullptr; }
}  // namespace

// The scope a mod condition or a skill's base expression runs in. VM-owned names go to the VM (which
// does not call the host for them); the rules' own names are answered here. The environment's terms
// (tide, fog, daylight) are not ported: they read as undefined.
struct HostScope : Scope {
    Rules& R;
    explicit HostScope(Rules& r) : R(r) {}
    Val get(const std::string& n) override {
        if (n == "clock" || n == "hour" || n == "time" || n == "place" || n.rfind("var.", 0) == 0 || n.rfind("flag.", 0) == 0 || (R.vm_ && R.vm_->book().vars.count(n))) return R.vm_ ? R.vm_->get(n) : Val::undef();
        if (n == "xp") return Val::num(R.sheet.xp);
        if (n == "level") return Val::num(R.sheet.level);
        if (n == "points") return Val::num(R.sheet.points);
        size_t d = n.find('.'); std::string pre = d == std::string::npos ? "" : n.substr(0, d), k = d == std::string::npos ? "" : n.substr(d + 1);
        if (pre == "attr") return Val::num(R.attr(k));
        if (pre == "skill") return Val::num(R.skill(k));
        if (pre == "meter") return Val::num(R.meter(k));
        if (R.rs_.skills.count(n)) return Val::num(R.skill(n));
        if (std::find(R.rs_.attr_order.begin(), R.rs_.attr_order.end(), n) != R.rs_.attr_order.end()) return Val::num(R.attr(n));
        const Val* v = R.vm_ ? R.vm_->raw_var(n) : nullptr; return v ? *v : Val::undef();
    }
    Val call(const std::string& n, const std::vector<NodeP>& a) override {
        if (n == "skill" && !a.empty()) return Val::num(R.skill(val_str(arg_of(a[0]))));
        if (n == "attr" && !a.empty()) return Val::num(R.attr(val_str(arg_of(a[0]))));
        if (n == "meter" && !a.empty()) return Val::num(R.meter(val_str(arg_of(a[0]))));
        return R.vm_ ? R.vm_->call(n, a) : Val::undef();
    }
    void assign(const std::string&, const Val&) override {}
    bool is_word(const std::string& n, const std::string& w) override { return R.vm_ && R.vm_->is_word(n, w); }
};

Rules::Rules(const Book& book, uint32_t seed) : book_(book), rs_(load_ruleset(book)), seed_(seed) {
    for (auto& a : rs_.attr_order) sheet.attrs[a] = rs_.num("attr-min", 1);
}
double Rules::clock() const { return vm_ ? vm_->clock() : 0; }

bool Rules::worn(const std::string& id) const {
    auto it = rs_.items.find(id); if (it == rs_.items.end() || it->second.slot.empty()) return false;
    auto e = sheet.equipped.find(it->second.slot); return e != sheet.equipped.end() && e->second == id;
}
void Rules::give(const std::string& id, double n) { sheet.items[id] += n; }
bool Rules::equip(const std::string& id) {
    auto it = rs_.items.find(id); if (it == rs_.items.end() || it->second.slot.empty()) return false;
    auto c = sheet.items.find(id); if (c == sheet.items.end() || !c->second) return false;
    HostScope S(*this); if (it->second.requires_ && !evaluate(it->second.requires_, S)) return false;
    sheet.equipped[it->second.slot] = id; return true;
}
// become(): the archetype's attributes, ranks and items replace the sheet's (rules/creation.mjs).
bool Rules::become(const std::string& id) {
    auto a = rs_.archetypes.find(id); if (a == rs_.archetypes.end()) throw std::runtime_error("become: no archetype " + id);
    if (sheet.archetype == id) return false;
    for (auto& k : rs_.attr_order) sheet.attrs[k] = rs_.num("attr-min", 1);
    for (auto& [k, v] : a->second.attrs) sheet.attrs[k] = v;
    sheet.ranks.clear(); for (auto& [k, v] : a->second.skills) sheet.ranks[k] = v;
    sheet.archetype = id;
    for (auto& [it, n] : a->second.items) { give(it, n); auto d = rs_.items.find(it); if (d != rs_.items.end() && !d->second.slot.empty()) equip(it); }
    return true;
}
void Rules::start(const std::string& archetype) { if (!archetype.empty() && rs_.archetypes.count(archetype)) become(archetype); }

std::vector<Mod> Rules::active(const std::string& target) {
    std::vector<Mod> out; HostScope S(*this);
    for (auto& [id, n] : sheet.items) {
        auto it = rs_.items.find(id); if (it == rs_.items.end() || !n) continue;
        if (!it->second.slot.empty() && !worn(id)) continue;
        for (auto& m : it->second.mods) if (m.target == target && (!m.cond || evaluate(m.cond, S))) out.push_back(m);
    }
    return out;
}
double Rules::attr(const std::string& id) {
    auto b = sheet.attrs.find(id); double v = b == sheet.attrs.end() ? 0 : b->second;
    for (auto& m : active("attr." + id)) v += m.amount;
    return v;
}
double Rules::skill_base(const std::string& id) {
    auto s = rs_.skills.find(id); if (s == rs_.skills.end()) return 0;
    HostScope S(*this);
    if (s->second.base) return js_num(S.eval(s->second.base->ast));
    return s->second.attr.empty() ? 0 : attr(s->second.attr);
}
// Everything that makes up the number for a check: base, training, item mods, the environment's @modifier records, extra.
double Rules::breakdown(const std::string& skill, double extra) {
    HostScope S(*this); std::vector<double> parts;
    parts.push_back(skill_base(skill));
    auto r = sheet.ranks.find(skill); if (r != sheet.ranks.end() && r->second) parts.push_back(r->second);
    for (auto& m : active("skill." + skill)) parts.push_back(m.amount);
    for (auto& m : rs_.modifiers) {
        bool hit = false; for (auto& s : m.skills) if (s == "*" || s == skill) hit = true;
        if (hit && (!m.cond || evaluate(m.cond, S))) parts.push_back(m.amount);
    }
    if (extra) parts.push_back(extra);
    double t = 0; for (double p : parts) t += p; return t;
}
double Rules::skill(const std::string& id) { return breakdown(id, 0); }

std::string Rules::label(double dc) const {
    std::string l, spec = rs_.str("dc-labels");
    if (spec.empty()) { for (auto& e : DEFAULT_LABELS) if (dc >= std::atof(e[0])) l = e[1]; return l; }
    std::vector<std::string> ents; std::string c;
    for (char ch : spec + ",") { if (ch == ',') { ents.push_back(c); c.clear(); } else c += ch; }
    for (auto& e : ents) {
        size_t a = e.find_first_not_of(" \t"); if (a == std::string::npos) continue;
        size_t sp = e.find_first_of(" \t", a), w = sp == std::string::npos ? std::string::npos : e.find_first_not_of(" \t", sp);
        if (dc >= std::atof(e.substr(a, sp - a).c_str())) l = w == std::string::npos ? "" : e.substr(w);
    }
    return l;
}

Json Rules::passive(const Json& a) {
    double dc = dnum(at(a, 2)); bool pass = breakdown(dstr(at(a, 1)), 0) >= dc;
    return Json::obj().set("dc", Json::num(dc)).set("label", Json::str(label(dc))).set("pass", Json::boolean(pass));
}
Json Rules::preview(const Json& a) {
    std::string key = dstr(at(a, 0)), skill = dstr(at(a, 1)), retry = at(a, 3) && at(a, 3)->k == Json::Str ? at(a, 3)->s : "raise";
    double strain = at(a, 4) && at(a, 4)->get("strain") ? dnum(at(a, 4)->get("strain")) : 0;
    double total = breakdown(skill, strain); auto rec = checks_.find(key); bool attempted = rec != checks_.end();
    Json locked;
    if (attempted && !rec->second.pass) {
        double t = total - strain; auto sk = rs_.skills.find(skill); std::string nm = sk != rs_.skills.end() ? sk->second.name : skill;
        bool fixed = rs_.str("check-mode") == "fixed"; double ranks = sheet.ranks.count(skill) ? sheet.ranks[skill] : 0;
        if (retry == "never" || retry == "red") locked = Json::str("Only one try");
        else if (retry == "raise" && t <= rec->second.total) locked = Json::str(fixed ? "Nothing has changed." : "Try again when your " + nm + " is higher");
        else if (retry == "skill" && ranks <= rec->second.ranks) locked = Json::str("Try again after training " + nm);
        else if (retry.rfind("time:", 0) == 0) { double left = rec->second.at + std::atof(retry.c_str() + 5) - clock(); if (left > 0) locked = Json::str("Try again in " + dump_number(std::ceil(left)) + " minutes"); }
    }
    return Json::obj().set("attempts", Json::num(attempted ? rec->second.attempts : 0)).set("locked", locked).set("passed", Json::boolean(attempted && rec->second.pass));
}
Json Rules::resolve(const Json& a) {
    std::string key = dstr(at(a, 0)), skill = dstr(at(a, 1)); double dc = dnum(at(a, 2));
    const Json* o = at(a, 3); bool quiet = o && o->get("passive") && o->get("passive")->b; double strain = dnum(o ? o->get("strain") : nullptr);
    double total = breakdown(skill, strain); CheckRec& rec = checks_[key]; bool pass; Json roll;
    if (rs_.str("check-mode") == "fixed") pass = total >= dc;
    else {
        double d[2]; for (int i = 0; i < 2; i++) d[i] = 1 + std::floor(rand01(seed_, key + "|" + dump_number(rec.attempts) + "#" + std::to_string(i)) * 6);
        roll = Json::arr().add(Json::num(d[0])).add(Json::num(d[1])); double s = d[0] + d[1];
        pass = s == 12 ? true : s == 2 ? false : s + total >= dc;
    }
    if (!quiet) { rec.attempts += 1; rec.pass = pass; rec.total = total - strain; rec.ranks = sheet.ranks.count(skill) ? sheet.ranks[skill] : 0; rec.at = clock(); }
    return Json::obj().set("dc", Json::num(dc)).set("label", Json::str(label(dc))).set("pass", Json::boolean(pass)).set("roll", roll).set("total", Json::num(total));
}

}  // namespace raw::game::lowwater
