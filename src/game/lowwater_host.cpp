// Which module answers a host call, the state changes a call makes, and the meters
// (world/pressure.mjs in Low Water).
#include "raw/game/lowwater/rules.hpp"
#include <algorithm>

namespace raw::game::lowwater {
using namespace raw::story;

namespace {
std::string arg0(const Json& a) { return !a.a.empty() && a.a[0].k == Json::Str ? a.a[0].s : ""; }
std::string prefix(const std::string& n) { size_t d = n.find('.'); return d == std::string::npos || d == 0 ? "" : n.substr(0, d); }
// prefixes the scope resolves through other game systems that are not ported (factions, resources, cabinet, ...)
bool other_system(const std::string& p) {
    static const char* P[] = {"rep", "res", "trait", "quest", "project", "item", "run", "sneak", "seenby"};
    for (auto* x : P) if (p == x) return true;
    return false;
}
}  // namespace

Module Rules::module_of(const std::string& f, const Json& args) const {
    if (f == "checks.passive" || f == "checks.preview" || f == "checks.resolve" || f == "sheet.skill") return Module::Checks;
    if (f == "sheet.addXp" || f == "call:become") return Module::Sheet;
    if (f == "pressure.takeFull" || f == "pressure.afterFull") return Module::Meters;
    if (f == "get" || f == "assign") {
        std::string n = arg0(args), p = prefix(n);
        if (p == "meter") return Module::Meters;
        if (f == "assign") return Module::None;
        if (p == "skill" || (p.empty() && rs_.skills.count(n))) return Module::Checks;
        if (p == "attr" || (p.empty() && std::find(rs_.attr_order.begin(), rs_.attr_order.end(), n) != rs_.attr_order.end())) return Module::Sheet;
        if (other_system(p)) return Module::None;
        return Module::Sheet;   // an undeclared name: the value an effect set, else undefined
    }
    return Module::None;
}

void Rules::observe(const std::string& f, const Json& args) {
    if (f == "call:become" && !args.a.empty() && !args.a[0].a.empty()) become(args.a[0].a[0].s);
    else if (f == "sheet.addXp" && !args.a.empty()) {
        sheet.xp += args.a[0].n;
        while (sheet.xp >= (sheet.level) * rs_.num("xp-per-level", 100)) { sheet.level++; sheet.points += rs_.num("points-per-level", 1); }   // xpForLevel(level + 1)
    }
    else if (f == "assign" && prefix(arg0(args)) == "meter" && args.a.size() > 1) { std::string id = arg0(args).substr(6); add_meter(id, args.a[1].n - meter(id)); }
}

Json Rules::answer(const std::string& f, const Json& args) {
    if (f == "checks.passive") return passive(args);
    if (f == "checks.preview") return preview(args);
    if (f == "checks.resolve") return resolve(args);
    if (f == "sheet.skill") return Json::num(skill(arg0(args)));
    if (f == "pressure.takeFull") return take_full();
    if (f == "pressure.afterFull") { after_full(arg0(args)); return Json(); }
    if (f == "get") {
        std::string n = arg0(args), p = prefix(n), k = p.empty() ? n : n.substr(p.size() + 1);
        if (p == "skill") return Json::num(skill(k));
        if (p == "attr") return Json::num(attr(k));
        if (p == "meter") return Json::num(meter(k));
        if (p.empty() && rs_.skills.count(n)) return Json::num(skill(n));
        if (p.empty() && std::find(rs_.attr_order.begin(), rs_.attr_order.end(), n) != rs_.attr_order.end()) return Json::num(attr(n));
        const Val* v = vm_ ? vm_->raw_var(n) : nullptr; return v ? to_json(*v) : Json();
    }
    return Json();   // become, addXp and assign answer null
}

double Rules::meter(const std::string& id) const {
    auto m = rs_.meters.find(id); if (m == rs_.meters.end()) return 0;
    auto v = meters_.find(id); return v == meters_.end() ? m->second.start : v->second;
}
void Rules::add_meter(const std::string& id, double n) {
    auto m = rs_.meters.find(id); if (m == rs_.meters.end()) throw std::runtime_error("no meter " + id);
    const MeterDef& d = m->second; double before = meter(id), after = std::max(d.min, std::min(d.max, before + n));
    meters_[id] = after;
    if (after >= d.max && !full_[id] && !d.full.empty()) { full_[id] = true; queue_.push_back(id); }
    else if (after < d.max && full_[id]) full_.erase(id);
}
// The next full meter whose scene should play, with its node resolved; null when none.
Json Rules::take_full() {
    while (!queue_.empty()) {
        std::string id = queue_.front(); queue_.erase(queue_.begin()); std::string node = full_node(id);
        if (!node.empty()) return Json::obj().set("id", Json::str(id)).set("node", Json::str(node));
    }
    return Json();
}
// "$district" and "$trait" need the world map and the cabinet, which are not ported: the district reads
// as empty and no trait is complete, so $trait tries "default" only.
std::string Rules::full_node(const std::string& id) const {
    auto m = rs_.meters.find(id); if (m == rs_.meters.end() || m->second.full.empty()) return "";
    std::string f = m->second.full, g = f;
    size_t t = g.find("$trait"); if (t != std::string::npos) g.replace(t, 6, "default");
    size_t d = g.find("$district"); if (d != std::string::npos) g.replace(d, 9, "");
    const auto& nodes = book_.nodes;
    if (nodes.count(g)) return g;
    std::string bare = f.substr(0, f.find(".$"));
    return nodes.count(bare) ? bare : "";
}
// After the full scene the meter takes its `after` value. The minutes the scene waits are a time record
// the host made, which the replay applies from the golden trace.
void Rules::after_full(const std::string& id) {
    auto m = rs_.meters.find(id); if (m == rs_.meters.end()) return;
    if (m->second.has_after) add_meter(id, m->second.after - meter(id));
}

}  // namespace raw::game::lowwater
