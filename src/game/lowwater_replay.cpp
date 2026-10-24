#include "raw/game/lowwater/replay.hpp"
#include <sstream>

namespace raw::game::lowwater {
using namespace raw::story;
namespace {

struct Recorded : Host {
    std::vector<Json> stream; std::vector<size_t> ks; size_t vmcount = 0; size_t pos = 0; Trace* tr = nullptr; Vm* vm = nullptr; uint32_t seed = 0;
    std::string error; size_t calls = 0;
    static bool host_tagged(const Json& r) { return r.str_or("t") == "ev" || r.str_or("s") == "h"; }
    void fail(const std::string& m) { if (error.empty()) error = m; }
    void apply(const Json& r) {   // a record the host made: apply it to the VM state and pass it through
        std::string t = r.str_or("t");
        if (t == "flag") { const Json* on = r.get("on"); vm->host_flag(r.str_or("id"), on && on->n != 0, false); }
        else if (t == "var") { const Json* v = r.get("v"); vm->host_var(r.str_or("n"), v ? from_json(*v) : Val::undef()); }
        else if (t == "time") { const Json* to = r.get("to"); if (to) vm->host_advance(to->n); }
        else if (t == "place") vm->host_place(r.str_or("id"));
        tr->emit(r);
    }
    void vm_record() override { vmcount++; drain(); }
    void drain() override { while (pos < stream.size() && host_tagged(stream[pos]) && ks[pos] <= vmcount) { Json r = stream[pos++]; apply(r); } }
    bool pending(std::string& kind) override {
        drain();
        if (tr->frozen || pos >= stream.size() || stream[pos].str_or("t") != "h") return false;
        std::string f = stream[pos].str_or("f");
        if (f == "runs.take" || f == "combat.take" || f == "sneaks.take") { kind = f; return true; }
        return false;
    }
    Json call(const std::string& f, const Json& args) override {
        calls++;
        if (tr->frozen) return Json();
        while (pos < stream.size() && stream[pos].str_or("t") != "h") {
            Json r = stream[pos];
            if (r.str_or("t") == "rng") {
                const Json* sd = r.get("seed"); std::string key = r.str_or("key");
                tr->emit(Json::obj().set("t", Json::str("rng")).set("seed", Json::num(sd ? sd->n : 0)).set("key", Json::str(key)).set("v", Json::num(rand01((uint32_t)(sd ? sd->n : 0), key))));
                pos++;
            } else if (host_tagged(r)) { pos++; apply(r); }
            else { fail("unexpected record in the host stream: " + dump_json(r)); pos++; }
        }
        if (pos >= stream.size()) { fail("the VM made a host call the golden trace does not have: " + f + " " + dump_json(args)); return Json(); }
        Json h = stream[pos];
        if (h.str_or("f") != f || dump_json(*h.get("a")) != dump_json(args)) {
            fail("host call mismatch: VM " + f + " " + dump_json(args) + " vs golden " + h.str_or("f") + " " + dump_json(*h.get("a")));
            return Json();
        }
        Json r = *h.get("r"); pos++;
        tr->emit(Json::obj().set("t", Json::str("h")).set("f", Json::str(f)).set("a", args).set("r", r));
        drain();
        return r;
    }
};

}  // namespace

ReplayResult replay(const std::string& golden, const std::vector<Source>& sources, const std::string& until) {
    ReplayResult R; Trace tr; tr.until = until;
    std::vector<Json> driver; Recorded host; host.tr = &tr;
    std::istringstream in(golden); std::string line; size_t vmseen = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Json j = parse_json(line); std::string t = j.str_or("t");
        if (t == "begin" || t == "talk" || t == "take" || t == "after" || t == "next") { driver.push_back(j); vmseen++; }
        else if (t == "h" || t == "rng" || t == "ev" || j.str_or("s") == "h") { host.stream.push_back(j); host.ks.push_back(vmseen); }
        else if (t != "stop") vmseen++;
    }
    try {
        Book book = load_book(sources);
        if (driver.empty() || driver[0].str_or("t") != "begin") { R.error = "the golden trace has no begin record"; return R; }
        const Json& b = driver[0]; const Json* sd = b.get("seed");
        host.seed = (uint32_t)(sd ? sd->n : 0);
        Vm vm(book, host, tr, host.seed); host.vm = &vm;
        std::vector<std::string> flags; if (auto* f = b.get("flags")) for (auto& x : f->a) flags.push_back(x.s);
        std::set<std::string> exits; if (auto* e = b.get("exits")) for (auto& x : e->a) exits.insert(x.s);
        const Json* vars = b.get("vars"); const Json* ck = b.get("clock");
        vm.init(ck ? ck->n : 0, b.str_or("place"), vars ? *vars : Json::obj(), flags, exits);
        tr.emit(b); host.vm_record();
        for (size_t d = 1; d < driver.size() && host.error.empty() && !tr.frozen; d++) {
            const Json& r = driver[d]; std::string t = r.str_or("t");
            host.drain();
            if (t == "talk") { tr.emit(Json::obj().set("t", Json::str("talk")).set("node", Json::str(r.str_or("node"))).set("clock", Json::num(vm.clock()))); host.vm_record(); vm.start(r.str_or("node")); }
            else if (t == "take") {
                const Json* i = r.get("i"); int idx = i ? (int)i->n : 0;
                std::string key = idx >= 0 && idx < (int)vm.beat().options.size() ? vm.beat().options[idx].key : "";
                tr.emit(Json::obj().set("t", Json::str("take")).set("i", Json::num(idx)).set("key", Json::str(key))); host.vm_record(); vm.choose(idx);
            }
            else if (t == "after") { tr.emit(r); host.vm_record(); vm.after(r.str_or("k")); }
            else if (t == "next") { tr.emit(r); host.vm_record(); vm.next_scene(); }
        }
        host.drain();
    } catch (std::exception& e) { R.error = std::string("VM error: ") + e.what(); }
    if (R.error.empty() && !tr.frozen) R.error = host.error;
    if (tr.frozen) R.error.clear();
    for (auto& l : tr.lines) { R.trace += l; R.trace += '\n'; }
    R.host_calls = host.calls; R.vm_records = tr.lines.size();
    return R;
}

size_t first_difference(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b); std::string l1, l2; size_t n = 0;
    for (;;) {
        bool g1 = (bool)std::getline(x, l1), g2 = (bool)std::getline(y, l2); n++;
        if (!g1 && !g2) return 0;
        if (g1 != g2 || l1 != l2) return n;
    }
}

}  // namespace raw::game::lowwater
