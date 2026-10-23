// The story VM: a port of dialogue/runner.mjs and the scope in core/scope.mjs of Low Water, for the part
// of them that does not need the rules. Same evaluation order, so the same host calls in the same order.
#include "raw/story/story.hpp"
#include <algorithm>
#include <cmath>

namespace raw::story {
namespace {
const std::set<std::string> VM_CALLS = {"has", "at", "seen", "visits", "said", "set", "clear", "min", "max", "abs", "wait", "cut"};
const std::set<std::string> HOST_PREFIX = {"rep", "res", "meter", "attr", "skill"};
const int MAX_HOPS = 200;
std::string as_str(const Val& v) { return v.k == Val::S ? v.s : v.k == Val::N ? dump_number(v.n) : v.k == Val::B ? (v.b ? "true" : "false") : "undefined"; }
Json jarr(std::initializer_list<Json> l) { Json a = Json::arr(); for (auto& x : l) a.add(x); return a; }
}  // namespace

Vm::Vm(const Book& book, Host& host, Trace& trace, uint32_t seed) : book_(book), host_(host), tr_(trace), seed_(seed) {}

void Vm::init(double clock, const std::string& place, const Json& vars, const std::vector<std::string>& flags, const std::set<std::string>& exits) {
    clock_ = clock; place_ = place; flags_ = flags; exits_ = exits;
    for (auto& [k, v] : vars.o) vars_[k] = from_json(v);
}

bool Vm::has_flag(const std::string& id) const { return std::find(flags_.begin(), flags_.end(), id) != flags_.end(); }

void Vm::set_flag(const std::string& id, bool on) {
    bool has = has_flag(id);
    if (on && !has) {
        flags_.push_back(id);
        auto f = book_.facts.find(id);
        if (f != book_.facts.end() && !f->second.one("about").empty() && f->second.one("journal") != "no") host_.drain();   // the journal line a fact writes is the host's
        emit(Json::obj().set("t", Json::str("flag")).set("id", Json::str(id)).set("on", Json::num(1)));
    } else if (!on && has) {
        flags_.erase(std::find(flags_.begin(), flags_.end(), id));
        emit(Json::obj().set("t", Json::str("flag")).set("id", Json::str(id)).set("on", Json::num(0)));
    }
}
void Vm::host_flag(const std::string& id, bool on, bool emit_rec) {
    bool has = has_flag(id);
    if (on && !has) flags_.push_back(id); else if (!on && has) flags_.erase(std::find(flags_.begin(), flags_.end(), id));
    (void)emit_rec;
}
void Vm::host_var(const std::string& n, const Val& v) {
    size_t d = n.find('.'); std::string pre = d != std::string::npos && d > 0 ? n.substr(0, d) : "";
    vars_[pre == "var" ? n.substr(d + 1) : n] = v;
}
void Vm::set_place(const std::string& p) {
    if (place_ == p) return;
    place_ = p; emit(Json::obj().set("t", Json::str("place")).set("id", Json::str(p)));
}
void Vm::advance(double minutes, bool cut) {
    if (!(minutes > 0)) return;
    double before = clock_; clock_ += minutes;
    Json j = Json::obj().set("t", Json::str("time")).set("from", Json::num(before)).set("to", Json::num(clock_));
    if (cut) j.set("cut", Json::num(1));
    emit(j);
}
Val Vm::var_get(const std::string& k) {
    auto it = vars_.find(k); if (it != vars_.end()) return it->second;
    auto d = book_.vars.find(k); return d != book_.vars.end() ? d->second.def : Val::undef();
}

Val Vm::get(const std::string& name) {
    if (name == "clock" || name == "hour" || name == "time") return Val::num(clock_);
    if (name == "place") return Val::str(place_);
    if (name.rfind("var.", 0) == 0) return var_get(name.substr(4));
    if (name.rfind("flag.", 0) == 0) return Val::boolean(has_flag(name.substr(5)));
    if (book_.vars.count(name)) return var_get(name);
    return from_json(host_.call("get", jarr({Json::str(name)})));
}
void Vm::assign(const std::string& name, const Val& v) {
    size_t d = name.find('.'); std::string pre = d != std::string::npos && d > 0 ? name.substr(0, d) : "";
    if (HOST_PREFIX.count(pre)) { host_.call("assign", jarr({Json::str(name), to_json(v)})); return; }
    vars_[pre == "var" ? name.substr(d + 1) : name] = v;
    emit(Json::obj().set("t", Json::str("var")).set("n", Json::str(name)).set("v", to_json(v)));
}
bool Vm::is_word(const std::string& name, const std::string& word) {
    auto it = book_.vars.find(name.rfind("var.", 0) == 0 ? name.substr(4) : name);
    if (it == book_.vars.end()) return false;
    return std::find(it->second.values.begin(), it->second.values.end(), word) != it->second.values.end();
}

Val Vm::call(const std::string& name, const std::vector<NodeP>& args) {
    if (VM_CALLS.count(name)) {
        std::vector<Val> ids, vals;
        for (auto& a : args) ids.push_back(arg_of(a));
        for (auto& a : args) vals.push_back(value_of(a));
        auto id = [&]() { return ids.empty() ? std::string("undefined") : as_str(ids[0]); };
        if (name == "has") return Val::boolean(has_flag(id()));
        if (name == "at") return Val::boolean(place_ == id());
        if (name == "seen") return Val::boolean(visits_[id()] > 0);
        if (name == "visits") return Val::num(visits_[id()]);
        if (name == "said") return Val::boolean(said_.count(id()) > 0);
        if (name == "set") { set_flag(id(), true); return Val::undef(); }
        if (name == "clear") { set_flag(id(), false); return Val::undef(); }
        if (name == "wait") { advance(vals.empty() ? 0 : to_num(vals[0])); return Val::undef(); }
        if (name == "cut") { advance(vals.empty() ? 0 : to_num(vals[0]), true); return Val::undef(); }
        if (name == "abs") return Val::num(std::abs(vals.empty() ? 0 : to_num(vals[0])));
        double r = name == "min" ? 1e300 : -1e300;
        for (auto& v : vals) r = name == "min" ? std::min(r, to_num(v)) : std::max(r, to_num(v));
        return Val::num(r);
    }
    Json a = Json::arr();
    for (auto& x : args) a.add(x->k == "id" ? Json::str(x->name) : x->k == "lit" ? to_json(x->lit) : Json::str("<expr>"));
    return from_json(host_.call("call:" + name, jarr({a})));
}

void Vm::line(const std::string& kind, const std::string& who, const std::string& text) {
    std::string t = text;
    if (t.find("{{") != std::string::npos) { Json r = host_.call("fill", jarr({Json::str(t)})); if (r.k == Json::Str) t = r.s; }
    emit(Json::obj().set("t", Json::str("line")).set("k", Json::str(kind)).set("who", Json::str(who)).set("text", Json::str(t)).set("clock", Json::num(clock_)).set("node", Json::str(node_)));
}

bool Vm::shows(const Step& s) {
    if (s.once && heard_.count(s.key)) return false;
    if (s.cond && !evaluate(s.cond, *this)) return false;
    if (s.once) heard_.insert(s.key);
    return true;
}
void Vm::voice(const Step& s) {
    if (s.once && heard_.count(s.key)) return;
    if (s.cond && !evaluate(s.cond, *this)) return;
    auto sk = book_.skills.find(s.skill);
    if (s.has_dc) { Json r = host_.call("checks.passive", jarr({Json::str(s.key), Json::str(s.skill), Json::num(s.dc)})); const Json* p = r.get("pass"); if (!(p && p->k == Json::Bool && p->b)) return; }
    if (s.has_below) { Json r = host_.call("sheet.skill", jarr({Json::str(s.skill)})); if (r.k == Json::Num && r.n >= s.below) return; }
    heard_.insert(s.key);
    line("voice", sk != book_.skills.end() ? sk->second.one("name") : s.skill, s.text);
}

bool Vm::option(const Step& s, Opt& out) {
    bool once_attr = s.attrs.count("once") && s.attrs.at("once").truthy();
    bool once = once_attr || s.kind == "check";
    if (s.kind != "claim" && s.cond && !evaluate(s.cond, *this)) return false;
    if (s.show && !evaluate(s.show, *this)) return false;
    if (once_attr && said_.count(s.key)) return false;
    out = Opt{s.key, s.kind, false, &s};
    if (s.kind == "check") {
        auto at = [&](const char* k) -> const Attr* { auto it = s.attrs.find(k); return it == s.attrs.end() ? nullptr : &it->second; };
        const Attr* skill = at("skill"); const Attr* dc = at("dc"); const Attr* retry = at("retry"); const Attr* red = at("red");
        double dcv = dc ? (dc->k == Attr::Num ? dc->n : std::atof(dc->s.c_str())) : std::nan("");
        std::string rt = retry && retry->truthy() ? retry->s : (red && red->truthy() ? "never" : "raise");
        Json r = host_.call("checks.preview", jarr({Json::str(s.key), Json::str(skill ? skill->s : ""), std::isnan(dcv) ? Json() : Json::num(dcv), Json::str(rt)}));
        auto t = [&](const char* k) { const Json* p = r.get(k); return p && truthy(from_json(*p)); };
        if (t("passed") && once) return false;
        if (t("attempts") && t("locked") && (rt == "never" || (red && red->truthy()))) return false;
        out.locked = t("locked");
    }
    return true;
}

std::string Vm::boundary(const std::string& id) {
    Json f = host_.call("pressure.takeFull", Json::arr());
    if (f.k == Json::Obj) {
        host_.call("pressure.afterFull", jarr({Json::str(f.str_or("id"))}));
        stack_.push_back({id, 0, true}); host_.drain(); return f.str_or("node");
    }
    if (const EventDef* e = due_event()) { fired_.insert(e->id); stack_.push_back({id, 0, true}); host_.drain(); return "@event." + e->id; }
    return id;
}
const EventDef* Vm::due_event() {
    for (auto& e : book_.events) if (!fired_.count(e.id) && clock_ >= e.at && (!e.when || evaluate(e.when, *this))) return &e;
    return nullptr;
}
std::string Vm::place_hub() {
    for (auto& id : book_.node_order) { const NodeDef& n = book_.nodes.at(id); if (n.hub) for (auto& s : n.steps) if (s.kind == "place" && s.value == place_) return id; }
    return "";
}

bool Vm::pending_beat(Beat& b) {
    std::string f;
    if (!host_.pending(f)) return false;
    b = Beat{}; b.pending = f; Json r = host_.call(f, Json::arr()); b.pendingId = r.k == Json::Str ? r.s : ""; return true;
}

Vm::Ret Vm::run_node(const std::string& id, int i, std::vector<Opt>& options, bool& have_beat) {
    auto nit = book_.nodes.find(id);
    if (nit == book_.nodes.end()) throw std::runtime_error("dialogue: no node " + id);
    const NodeDef& n = nit->second; node_ = id;
    if (i == 0) {
        visits_[id]++; emit(Json::obj().set("t", Json::str("node")).set("id", Json::str(id)).set("clock", Json::num(clock_)));
        if (n.hub) hub_ = id;
        speaker_ = n.speaker;
    }
    Ret R;
    for (; i < (int)n.steps.size(); i++) {
        const Step& s = n.steps[i]; const std::string& k = s.kind;
        if (k == "when") { if (!evaluate(s.cond, *this)) { R.next = "^return"; for (auto& x : n.steps) if (x.kind == "else") { R.next = x.value; break; } return R; } }
        else if (k == "cost") advance(s.dc);
        else if (k == "place") set_place(s.value);
        else if (k == "speaker") speaker_ = s.value;
        else if (k == "set" || k == "clear" || k == "do") run_effects(s.effects, *this);
        else if (k == "if") { if (evaluate(s.cond, *this)) run_effects(s.effects, *this); }
        else if (k == "cmd") { Json a = Json::arr(); for (auto& x : s.args) a.add(Json::str(x)); host_.call("command", jarr({a})); }
        else if (k == "nar") { if (shows(s)) line("nar", "", s.value); }
        else if (k == "line" || k == "inst") { if (shows(s)) { std::string who = s.who.empty() ? n.speaker : s.who; if (k == "line" && !s.who.empty()) speaker_ = s.who; line(k, who, s.text); } }
        else if (k == "read") {
            Json a = Json::arr(); std::string cur; for (char c : s.value + " ") { if (std::isspace((unsigned char)c)) { if (!cur.empty()) a.add(Json::str(cur)), cur.clear(); } else cur += c; }
            Json r = host_.call("read", jarr({a})); if (r.k == Json::Obj) line("inst", r.str_or("who"), r.str_or("text"));
        }
        else if (k == "voice") voice(s);
        else if (k == "passive") {
            if ((s.once && heard_.count(s.key)) || (s.cond && !evaluate(s.cond, *this))) continue;
            if (s.has_dc) { Json r = host_.call("checks.passive", jarr({Json::str(s.key), Json::str(s.skill), Json::num(s.dc)})); const Json* p = r.get("pass"); if (!(p && p->k == Json::Bool && p->b)) continue; }
            heard_.insert(s.key); stack_.push_back({n.id, i + 1, false}); R.next = s.target; return R;
        }
        else if (k == "alt") { if (evaluate(s.cond, *this)) { R.next = s.target; return R; } }
        else if (k == "go") { R.next = s.value; return R; }
        else if (k == "call") { stack_.push_back({n.id, i + 1, false}); R.next = s.value; return R; }
        else if (k == "return") { R.next = "^return"; return R; }
        else if (k == "end") {
            talk_done_ = true; emit(Json::obj().set("t", Json::str("end")).set("id", Json::str(s.value)));
            Beat b; b.end = s.value; b.done = true; beat_ = b; have_beat = true; return R;
        }
        else if (k == "exit") {
            int kk = -1; for (int x = (int)stack_.size() - 1; x >= 0; x--) if (stack_[x].interrupt) { kk = x; break; }
            if (kk >= 0) { Frame f = stack_[kk]; stack_.resize(kk); R.next = f.id; R.at = f.at; return R; }
            R.next = "^exit:" + s.value; return R;
        }
        else if (k == "opt" || k == "claim" || k == "check") { Opt o; if (option(s, o)) options.push_back(o); }
        Beat b;
        if (pending_beat(b)) { beat_ = b; have_beat = true; return R; }
    }
    if (!options.empty()) { Beat b; b.options = options; beat_ = b; have_beat = true; return R; }
    if (!stack_.empty()) { Frame f = stack_.back(); stack_.pop_back(); R.next = f.id; R.at = f.at; return R; }
    Beat b; b.deadEnd = id; b.done = true; talk_done_ = true; beat_ = b; have_beat = true; return R;
}

Beat& Vm::finish(Beat b) {
    talk_done_ = b.done; beat_ = std::move(b);
    if (talk_done_) host_.drain();
    return beat_;
}

Beat& Vm::play(std::string id, int at) {
    for (int hop = 0; hop < MAX_HOPS; hop++) {
        if (id == "^hub") { std::string hb = hub_.empty() ? place_hub() : hub_; if (hb.empty()) throw std::runtime_error("dialogue: ^hub with no hub visited"); id = hb; at = 0; }
        if (id == "^interrog") throw std::runtime_error("interrogations are not ported");
        if (id == "^return") {
            if (stack_.empty()) { Beat b; b.exit = "^return"; b.done = true; return finish(b); }
            Frame f = stack_.back(); stack_.pop_back(); id = f.id; at = f.at;
        }
        if (id.rfind("^exit:", 0) == 0 || exits_.count(id)) { Beat b; b.exit = id.rfind("^exit:", 0) == 0 ? id.substr(6) : id; b.done = true; return finish(b); }
        if (at == 0) id = boundary(id);
        std::vector<Opt> options; bool have = false;
        Ret r = run_node(id, at, options, have);
        if (have) { Beat b = beat_; return finish(b); }
        id = r.next; at = r.at;
    }
    throw std::runtime_error("dialogue: more than 200 hops without a choice, from " + start_id_);
}

void Vm::offer() {
    if (beat_.options.empty()) return;
    Json opts = Json::arr();
    for (auto& o : beat_.options) opts.add(Json::obj().set("key", Json::str(o.key)).set("kind", Json::str(o.kind)).set("locked", Json::num(o.locked ? 1 : 0)));
    emit(Json::obj().set("t", Json::str("offer")).set("node", Json::str(node_)).set("opts", opts));
}

const Beat& Vm::start(const std::string& node) {
    stack_.clear(); hub_.clear(); speaker_.clear(); start_id_ = node; talk_done_ = false;
    play(node, 0); offer(); return beat_;
}

const Beat& Vm::choose(int i) {
    if (i < 0 || i >= (int)beat_.options.size()) throw std::runtime_error("dialogue: no option " + std::to_string(i));
    Opt o = beat_.options[i]; const Step& s = *o.s;
    if (o.locked) return beat_;
    std::string label = s.label; if (!label.empty() && label.front() == '"') label.erase(0, 1); if (!label.empty() && label.back() == '"') label.pop_back();
    line("you", "LAMB", label);
    said_.insert(o.key);
    if (s.cost && !exits_.count(s.target)) advance(s.cost);
    if (s.effects) run_effects(s.effects, *this);
    Beat pb;
    if (pending_beat(pb)) { finish(pb); offer(); return beat_; }
    std::string target = s.target;
    auto xp = s.attrs.find("xp");
    if (s.kind == "claim") target = evaluate(s.cond, *this) ? s.target : s.fail;
    if (s.kind == "check") {
        const Attr& sk = s.attrs.at("skill"); auto dc = s.attrs.find("dc");
        double dcv = dc == s.attrs.end() ? std::nan("") : dc->second.k == Attr::Num ? dc->second.n : std::atof(dc->second.s.c_str());
        Json r = host_.call("checks.resolve", jarr({Json::str(s.key), Json::str(sk.s), std::isnan(dcv) ? Json() : Json::num(dcv), Json::obj().set("strain", Json::num(0))}));
        const Json* pass = r.get("pass"); bool ok = pass && pass->k == Json::Bool && pass->b;
        const Json* roll = r.get("roll");
        auto sn = book_.skills.find(sk.s); std::string skname = sn != book_.skills.end() ? sn->second.one("name") : sk.s;
        if (roll && roll->k != Json::Null) line("check", skname, r.str_or("label") + ": " + (ok ? "Success" : "Failure"));
        if (ok && xp != s.attrs.end()) host_.call("sheet.addXp", jarr({Json::num(xp->second.n)}));
        target = ok ? s.target : s.fail;
    } else if (xp != s.attrs.end()) host_.call("sheet.addXp", jarr({Json::num(xp->second.n)}));
    play(target, 0); offer(); return beat_;
}

const Beat& Vm::after(const std::string& kind) {
    if (kind == "afterSneak") {
        Json r = host_.call("sneaks.finish", Json::arr());
        std::string node = r.str_or("node");
        if (!node.empty()) { play(node, 0); offer(); return beat_; }
        Beat b; b.pending = "combat.take"; Json t = host_.call("combat.take", Json::arr()); b.pendingId = t.k == Json::Str ? t.s : ""; finish(b); return beat_;
    }
    Json r = host_.call(kind == "afterRun" ? "runs.finish" : "combat.finish", Json::arr());
    if (r.k != Json::Str) throw std::runtime_error("dialogue: the " + kind + " is not over");
    play(r.s, 0); offer(); return beat_;
}

bool Vm::next_scene() {
    if (!talk_done_) return false;
    Json f = host_.call("pressure.takeFull", Json::arr());
    std::string kind, id, node;
    if (f.k == Json::Obj) { host_.call("pressure.afterFull", jarr({Json::str(f.str_or("id"))})); kind = "meter"; id = f.str_or("id"); node = f.str_or("node"); }
    else if (const EventDef* e = due_event()) { fired_.insert(e->id); kind = "event"; id = e->id; node = "@event." + e->id; }
    else return false;
    emit(Json::obj().set("t", Json::str("scene")).set("kind", Json::str(kind)).set("id", Json::str(id)).set("node", Json::str(node)));
    return true;
}

}  // namespace raw::story
