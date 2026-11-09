// raw/story: a story VM for line-oriented dialogue scripts (the LWD/1 format of Low Water) that
// is not specific to one game. A Book is parsed from text; a Vm runs it against a Host that
// answers everything the VM does not own (rules, world, combat). The VM writes a canonical
// JSON-lines trace (docs/story-trace.md) so a run can be compared byte for byte with another
// implementation. Layer: story (depends on core and math only).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace raw::story {

// ---------------------------------------------------------------- JSON (ordered, canonical dump)
struct Json {
    enum K { Null, Bool, Num, Str, Arr, Obj } k = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    static Json num(double v) { Json j; j.k = Num; j.n = v; return j; }
    static Json str(std::string v) { Json j; j.k = Str; j.s = std::move(v); return j; }
    static Json boolean(bool v) { Json j; j.k = Bool; j.b = v; return j; }
    static Json arr() { Json j; j.k = Arr; return j; }
    static Json obj() { Json j; j.k = Obj; return j; }
    Json& add(Json v) { a.push_back(std::move(v)); return *this; }
    Json& set(const std::string& key, Json v) { o.emplace_back(key, std::move(v)); return *this; }
    const Json* get(const std::string& key) const { for (auto& p : o) if (p.first == key) return &p.second; return nullptr; }
    std::string str_or(const std::string& key, const std::string& d = "") const { auto* p = get(key); return p && p->k == Str ? p->s : d; }
};
Json parse_json(const std::string& text);              // throws std::runtime_error
std::string dump_json(const Json& j);                  // same bytes as JSON.stringify
std::string dump_number(double v);
std::string json_string(const std::string& s);
uint32_t hash_str(const std::string& utf8, uint32_t seed = 0x9e3779b9u);   // core/rng.mjs hashStr, bit for bit
double rand01(uint32_t seed, const std::string& key);                       // hashStr / 2^32

// ---------------------------------------------------------------- values and expressions
struct Val {
    enum K { U, B, N, S } k = U; bool b = false; double n = 0; std::string s;
    static Val undef() { return {}; }
    static Val boolean(bool v) { Val x; x.k = B; x.b = v; return x; }
    static Val num(double v) { Val x; x.k = N; x.n = v; return x; }
    static Val str(std::string v) { Val x; x.k = S; x.s = std::move(v); return x; }
};
bool truthy(const Val& v);
double to_num(const Val& v);
Val from_json(const Json& j);
Json to_json(const Val& v);

struct Node;
using NodeP = std::shared_ptr<Node>;
struct Node {
    std::string k, op, name, str; double num = 0; Val lit;
    NodeP a, b; std::vector<NodeP> args;
    std::vector<NodeP> list;           // effects: assign / call statements
};
struct Expr { std::string src; NodeP ast; };
using ExprP = std::shared_ptr<Expr>;

void set_clock_pivot(int hour);
int clock_pivot();
int pivot_for(int start_hour);
double to_min(const std::string& hhmm);
ExprP compile(const std::string& src);            // nullptr for empty; throws std::runtime_error on a syntax error
ExprP compile_effects(const std::string& src);

struct Scope {
    virtual ~Scope() = default;
    virtual Val get(const std::string& name) = 0;
    virtual Val call(const std::string& name, const std::vector<NodeP>& args) = 0;
    virtual void assign(const std::string& name, const Val& v) = 0;
    virtual bool is_word(const std::string& name, const std::string& word) = 0;
    Val eval(const NodeP& n);
    Val arg_of(const NodeP& a);        // a bare name is its name, anything else its value
    Val value_of(const NodeP& a);
};
bool evaluate(const ExprP& c, Scope& S);          // true for a null condition
void run_effects(const ExprP& c, Scope& S);

// ---------------------------------------------------------------- the book
struct Attr { enum K { None, Bool, Num, Str } k = None; bool b = true; double n = 0; std::string s;
    bool truthy() const { return k == Bool ? b : k == Num ? n != 0 : k == Str ? !s.empty() : false; } };
using Attrs = std::map<std::string, Attr>;
struct Step {
    std::string kind, key, value, label, target, fail, text, who, skill;
    int line = 0, cost = 0;
    ExprP cond, show, effects; bool once = false, has_dc = false, has_below = false;
    double dc = 0, below = 0; Attrs attrs; std::vector<std::string> args;
};
struct NodeDef { std::string id, speaker; bool hub = false, event = false; std::vector<Step> steps; std::string place; };
struct EventDef { std::string id; double at = -1e300; ExprP when; std::vector<Step> steps; };
struct Record { std::string kind, id; int line = 0; std::vector<std::pair<std::string, std::string>> kv;
    std::string one(const std::string& key, const std::string& d = "") const { for (auto& p : kv) if (p.first == key) return p.second; return d; } };
struct VarDecl { Val def; std::vector<std::string> values; };
struct Book {
    std::map<std::string, std::string> meta; std::vector<std::string> includes;
    std::map<std::string, NodeDef> nodes; std::vector<EventDef> events;
    std::map<std::string, VarDecl> vars; std::map<std::string, Record> skills; std::map<std::string, Record> facts;
    std::vector<std::string> errors, node_order; int records = 0;
    std::vector<Record> recs;          // every record that is not a node or an event, in file order (game layers read their own kinds)
    double clock0 = 23 * 60;
};
struct Source { std::string file, text; };
Book load_book(const std::vector<Source>& sources);
// The sources of an act: the root, then each `include:` of its @meta, relative to the root.
std::vector<Source> load_act_sources(const std::string& root_path, std::string (*read)(const std::string&));

// ---------------------------------------------------------------- the VM
// Everything the VM does not own is a host call: f names it, a holds its arguments, the answer is
// a Json value. See docs/story-trace.md for the list.
struct Host {
    virtual ~Host() = default;
    virtual Json call(const std::string& f, const Json& args) = 0;
    virtual bool pending(std::string& kind) = 0;      // a run, fight or sneak the last step started
    virtual void drain() = 0;
    virtual void vm_record() { drain(); }             // the VM just wrote a record of its own                         // apply the host's own records that follow a VM record
};
struct Trace {
    std::vector<std::string> lines; std::string until; bool frozen = false;   // until: stop after the first node whose id lacks this prefix
    void emit(const Json& j) {
        if (frozen) return;
        if (!until.empty() && j.str_or("t") == "node" && j.str_or("id").rfind(until, 0) != 0) {
            lines.push_back(dump_json(Json::obj().set("t", Json::str("stop")).set("id", Json::str(j.str_or("id"))))); frozen = true; return;
        }
        lines.push_back(dump_json(j));
    }
};

struct Opt { std::string key, kind; bool locked = false; const Step* s = nullptr; };
struct Beat { std::vector<Opt> options; std::string end, exit, deadEnd, pending, pendingId; bool done = false; };

class Vm : public Scope {
public:
    Vm(const Book& book, Host& host, Trace& trace, uint32_t seed);
    void init(double clock, const std::string& place, const Json& vars, const std::vector<std::string>& flags, const std::set<std::string>& exits);
    const Beat& start(const std::string& node);                 // a talk: the `talk` driver record
    const Beat& choose(int i);
    const Beat& after(const std::string& kind);                 // afterRun / afterFight / afterSneak
    bool next_scene();                                          // the `next` driver record
    void host_advance(double to) { clock_ = to; }
    void host_flag(const std::string& id, bool on, bool emit_rec);
    void host_var(const std::string& n, const Val& v);
    void host_place(const std::string& p) { place_ = p; }
    double clock() const { return clock_; }
    const Val* raw_var(const std::string& name) const { auto i = vars_.find(name); return i == vars_.end() ? nullptr : &i->second; }
    const Book& book() const { return book_; }
    bool done() const { return talk_done_; }
    const Beat& beat() const { return beat_; }
    const std::string& node() const { return node_; }
    // Scope
    Val get(const std::string& name) override;
    Val call(const std::string& name, const std::vector<NodeP>& args) override;
    void assign(const std::string& name, const Val& v) override;
    bool is_word(const std::string& name, const std::string& word) override;
    Json host(const std::string& f, const Json& a) { return host_.call(f, a); }
private:
    struct Frame { std::string id; int at; bool interrupt; };
    const Book& book_; Host& host_; Trace& tr_; uint32_t seed_;
    double clock_ = 0; std::string place_, node_, hub_, speaker_; std::vector<std::string> flags_;
    std::map<std::string, Val> vars_; std::map<std::string, int> visits_; std::set<std::string> said_, heard_, fired_, exits_;
    std::vector<Frame> stack_; Beat beat_; bool talk_done_ = true; std::string start_id_;
    void emit(const Json& j) { tr_.emit(j); host_.vm_record(); }
    bool has_flag(const std::string& id) const;
    void set_flag(const std::string& id, bool on);
    void set_place(const std::string& p);
    void advance(double minutes, bool cut = false);
    Val var_get(const std::string& k);
    void line(const std::string& kind, const std::string& who, const std::string& text);
    Beat& play(std::string id, int at);
    struct Ret { bool beat = false; std::string next; int at = 0; };
    Ret run_node(const std::string& id, int i, std::vector<Opt>& options, bool& have_beat);
    bool shows(const Step& s);
    void voice(const Step& s);
    bool option(const Step& s, Opt& out);
    std::string boundary(const std::string& id);
    Beat& finish(Beat b);
    void offer();
    const EventDef* due_event();
    std::string place_hub();
    bool pending_beat(Beat& b);
};

// RNG draws and trace records use these helpers.
inline std::string clock_str(double c) { return dump_number(c); }

}  // namespace raw::story
