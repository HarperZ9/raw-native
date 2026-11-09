// The ruleset: everything content defines besides dialogue, read from LWD records
// (rules/ruleset.mjs in Low Water). Names come from content; nothing here knows its attribute names.
#include "raw/game/lowwater/rules.hpp"
#include <algorithm>
#include <cstdlib>
#include <regex>

namespace raw::game::lowwater {
using namespace raw::story;
namespace {

std::string trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
// JS: isNaN(+v) ? v.trim() : +v   (an empty string reads as 0)
Val rule_value(const std::string& v) {
    std::string t = trim(v); if (t.empty()) return Val::num(0);
    char* e = nullptr; double x = std::strtod(t.c_str(), &e);
    return e && *e == 0 && t.find_first_of("xXnN") == std::string::npos ? Val::num(x) : Val::str(t);
}
double num_or(const std::string& v, double d) { std::string t = trim(v); if (t.empty()) return d; char* e = nullptr; double x = std::strtod(t.c_str(), &e); return e && *e == 0 ? x : d; }
std::vector<std::string> list(const std::string& v) {
    std::vector<std::string> out; std::string cur;
    for (char c : v + ",") { if (c == ',') { std::string t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); } else cur += c; }
    return out;
}
std::pair<std::string, double> count(const std::string& v) {
    std::string t = trim(v); size_t sp = t.find_first_of(" \t");
    return {t.substr(0, sp), sp == std::string::npos ? 1 : num_or(t.substr(sp), 1)};
}
std::string one(const Record& r, const std::string& k) { for (auto& p : r.kv) if (p.first == k) return trim(p.second); return ""; }
bool has(const Record& r, const std::string& k) { for (auto& p : r.kv) if (p.first == k) return true; return false; }
std::vector<std::string> all(const Record& r, const std::string& k) { std::vector<std::string> o; for (auto& p : r.kv) if (p.first == k) o.push_back(p.second); return o; }

}  // namespace

Mod parse_mod(const std::string& text, const std::string& source) {
    static const std::regex RE(R"(^([\w.*]+)\s*([+-]\s*\d+(?:\.\d+)?)(?:\s*\?\s*(.*))?$)");
    std::string t = trim(text); std::smatch m;
    if (!std::regex_match(t, m, RE)) throw std::runtime_error("mod: expected \"target +n [? cond]\", got " + text);
    std::string n; for (char c : m[2].str()) if (c != ' ' && c != '\t') n += c;
    Mod out; out.target = m[1]; out.amount = std::atof(n.c_str()); out.source = source;
    if (m[3].matched) out.cond = compile(m[3]);
    return out;
}

double Ruleset::num(const std::string& k, double d) const { auto i = rules.find(k); return i == rules.end() ? d : to_num(i->second); }
std::string Ruleset::str(const std::string& k) const { auto i = rules.find(k); if (i == rules.end()) return ""; return i->second.k == Val::S ? i->second.s : dump_number(i->second.n); }

Ruleset load_ruleset(const Book& book) {
    Ruleset rs;
    for (auto& [k, v] : std::vector<std::pair<const char*, double>>{{"xp-per-level", 100}, {"points-per-level", 1}, {"skill-cap", 4}, {"attr-min", 1}, {"strain-max", 3}, {"rep-min", -100}})
        rs.rules[k] = Val::num(v);
    rs.rules["check-mode"] = Val::str("dice");
    for (auto& r : book.recs) {
        try {
            if (r.kind == "rules") { for (auto& [k, v] : r.kv) rs.rules[k] = rule_value(v); }
            else if (r.kind == "attr") rs.attr_order.push_back(r.id);
            else if (r.kind == "skill") {
                SkillDef s; s.id = r.id; s.name = one(r, "name").empty() ? r.id : one(r, "name"); s.attr = one(r, "attr");
                if (!s.attr.empty() && std::find(rs.attr_order.begin(), rs.attr_order.end(), s.attr) == rs.attr_order.end()) throw std::runtime_error("attr " + s.attr + " is not defined");
                s.base = compile(one(r, "base")); rs.skills[r.id] = s;
            }
            else if (r.kind == "item") {
                ItemDef it; it.id = r.id; it.slot = one(r, "slot");
                for (auto& v : all(r, "mod")) it.mods.push_back(parse_mod(v, "item:" + r.id));
                it.requires_ = compile(one(r, "requires")); rs.items[r.id] = it;
            }
            else if (r.kind == "modifier") {
                ModifierDef m; m.skills = has(r, "skill") && !one(r, "skill").empty() ? list(one(r, "skill")) : std::vector<std::string>{"*"};
                m.amount = num_or(one(r, "mod"), 0); m.cond = compile(one(r, "when")); rs.modifiers.push_back(m);
            }
            else if (r.kind == "archetype") {
                ArchDef a; a.id = r.id; a.name = one(r, "name").empty() ? r.id : one(r, "name");
                for (auto& v : all(r, "attr")) a.attrs.push_back(count(v));
                for (auto& v : all(r, "skill")) a.skills.push_back(count(v));
                for (auto& v : all(r, "item")) a.items.push_back(count(v));
                rs.archetypes[r.id] = a;
            }
            else if (r.kind == "meter") {
                MeterDef m; m.id = r.id; m.full = one(r, "full");
                m.min = num_or(one(r, "min"), 0); m.max = num_or(one(r, "max"), 1); m.start = num_or(one(r, "start"), 0); m.wait = num_or(one(r, "wait"), 0);
                if (has(r, "after")) { m.has_after = true; m.after = num_or(one(r, "after"), 0); }
                rs.meters[r.id] = m;
            }
        } catch (std::exception& e) { rs.errors.push_back(r.kind + " " + r.id + ": " + e.what()); }
    }
    return rs;
}

}  // namespace raw::game::lowwater
