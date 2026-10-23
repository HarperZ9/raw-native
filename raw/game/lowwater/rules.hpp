// Low Water's rules, host side (step 3 of the C++ port): the ruleset read from a book's records, the
// character sheet, skill checks and meters. These are the answers the story VM asks its host for
// (docs/story-trace.md). Game-specific, so it lives here and not in raw/story. Ported from
// rules/{ruleset,sheet,checks,creation,inventory}.mjs and world/pressure.mjs of Low Water.
// Not ported (the host answers stay replayed): the cabinet's traits, factions, environment terms
// (tide, fog, daylight), districts and rumour spread, so the meter tick is not driven.
#pragma once
#include "raw/story/story.hpp"
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace raw::game::lowwater {
using raw::story::Json;

struct Mod { std::string target, source; double amount = 0; raw::story::ExprP cond; };
struct SkillDef { std::string id, name, attr; raw::story::ExprP base; };
struct ItemDef { std::string id, slot; std::vector<Mod> mods; raw::story::ExprP requires_; };
struct ArchDef { std::string id, name; std::vector<std::pair<std::string, double>> attrs, skills, items; };
struct ModifierDef { std::vector<std::string> skills; double amount = 0; raw::story::ExprP cond; };
struct MeterDef { std::string id, full; double min = 0, max = 1, start = 0, after = 0, wait = 0; bool has_after = false; };

struct Ruleset {
    std::map<std::string, raw::story::Val> rules;
    std::vector<std::string> attr_order;
    std::map<std::string, SkillDef> skills; std::map<std::string, ItemDef> items; std::map<std::string, ArchDef> archetypes;
    std::vector<ModifierDef> modifiers; std::map<std::string, MeterDef> meters;
    std::vector<std::string> errors;
    double num(const std::string& k, double d = 0) const;
    std::string str(const std::string& k) const;
};
Ruleset load_ruleset(const raw::story::Book& book);
Mod parse_mod(const std::string& text, const std::string& source);     // "skill.pride +1 ? cond"; throws on a bad line

struct Sheet {
    std::map<std::string, double> attrs, ranks, items; std::map<std::string, std::string> equipped;
    double xp = 0, level = 1, points = 0; std::string archetype;
};
struct CheckRec { double attempts = 0, total = std::nan(""), ranks = 0, at = 0; bool pass = false; };   // total is undefined until a try is recorded

// Which module answers a host call: the three that this file computes.
enum class Module { None, Checks, Sheet, Meters };

class Rules {
public:
    Rules(const raw::story::Book& book, uint32_t seed);
    void bind(raw::story::Vm* vm) { vm_ = vm; }
    void start(const std::string& archetype);              // the sheet the harness builds before the run
    Module module_of(const std::string& f, const Json& args) const;
    void observe(const std::string& f, const Json& args);   // state changes the host call makes (become, addXp, assign)
    Json answer(const std::string& f, const Json& args);    // the computed answer
    const Ruleset& ruleset() const { return rs_; }
    double skill(const std::string& id);                    // the effective level: the sum a check shows
    double attr(const std::string& id);
    double meter(const std::string& id) const;
    bool become(const std::string& id);
    void give(const std::string& id, double n);
    void add_meter(const std::string& id, double n);
    Json take_full();
    void after_full(const std::string& id);
    Sheet sheet;
private:
    friend struct HostScope;
    const raw::story::Book& book_; Ruleset rs_; uint32_t seed_; raw::story::Vm* vm_ = nullptr;
    std::map<std::string, CheckRec> checks_; std::map<std::string, double> meters_; std::map<std::string, bool> full_; std::vector<std::string> queue_;
    std::vector<Mod> active(const std::string& target);
    double skill_base(const std::string& id);
    double breakdown(const std::string& skill, double extra);
    std::string label(double dc) const;
    std::string full_node(const std::string& id) const;
    Json passive(const Json& a); Json preview(const Json& a); Json resolve(const Json& a);
    bool equip(const std::string& id);
    bool worn(const std::string& id) const;
    double clock() const;
    bool has_skill(const std::string& n) const { return rs_.skills.count(n) > 0; }
};

}  // namespace raw::game::lowwater
