// Story VM tests: JSON, the JS-compatible RNG, expressions, the parser, and trace equivalence with
// runs recorded by the JS engine (tests/data/story, an original fixture).
#include "check.hpp"
#include "raw/game/lowwater/replay.hpp"
#include <fstream>
#include <sstream>

using namespace raw::story;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/story/";

struct TestScope : Scope {
    std::map<std::string, Val> v;
    Val get(const std::string& n) override { auto i = v.find(n); return i == v.end() ? Val::undef() : i->second; }
    Val call(const std::string& n, const std::vector<NodeP>& a) override { return n == "twice" ? Val::num(2 * to_num(value_of(a[0]))) : Val::undef(); }
    void assign(const std::string& n, const Val& x) override { v[n] = x; }
    bool is_word(const std::string& n, const std::string& w) override { return n == "mood" && (w == "calm" || w == "sour"); }
};
static bool cond(TestScope& s, const char* src) { return evaluate(compile(src), s); }

int main() {
    // JSON
    { std::string nl(1, 10), bs(1, 92), q(1, 34); std::string j = "{" + q + "a" + q + ":[1,2.5," + q + "x" + bs + "n" + q + "]," + q + "b" + q + ":null," + q + "c" + q + ":true}"; CHECK(dump_json(parse_json(j)) == j); CHECK(parse_json(j).get("a")->a[2].s == "x" + nl); }
    CHECK(dump_number(1790) == "1790"); CHECK(dump_number(-0.0) == "0"); CHECK(dump_number(0.1) == "0.1"); CHECK(dump_number(1e-7) == "1e-7");
    { std::string in = std::string("a") + char(34) + "b" + char(92) + char(1), want = std::string(1, 34) + "a" + char(92) + char(34) + "b" + char(92) + char(92) + char(92) + "u0001" + char(34); CHECK(json_string(in) == want); }
    // RNG: values taken from core/rng.mjs in Low Water
    CHECK(hash_str("abc", 1) == 1956875506u);
    CHECK(rand01(7, "e1.tower@25#0") == 0.7072922186926007);
    CHECK(hash_str("\xC3\xA9\xF0\x9D\x84\x9Ex", 0x9e3779b9u) == 2023377644u);
    // expressions
    set_clock_pivot(12);
    TestScope s; s.v["tide"] = Val::num(-0.3); s.v["mood"] = Val::str("sour"); s.v["clock"] = Val::num(23 * 60 + 40);
    CHECK(cond(s, "tide < -0.27 & !(clock < 23:30)"));
    CHECK(cond(s, "mood == sour")); CHECK(!cond(s, "mood == calm"));
    CHECK(cond(s, "twice(3) == 6 | false"));
    CHECK(to_min("01:30") == 25 * 60 + 30);
    CHECK(!cond(s, "nothing")); CHECK(cond(s, "nothing == nothing"));
    auto fx = compile_effects("n = 2; n += 3; mood = calm");
    run_effects(fx, s); CHECK(to_num(s.v["n"]) == 5); CHECK(s.v["mood"].s == "calm");
    bool threw = false; try { compile("1 +"); } catch (std::exception&) { threw = true; } CHECK(threw);
    // parser
    Book b = load_book({{"t.lwd", slurp(DIR + "harbour.lwd")}});
    CHECK(b.errors.empty()); CHECK(b.nodes.count("pier.hub") && b.nodes["pier.hub"].hub);
    CHECK(b.nodes.count("@event.meta.at-end")); CHECK(b.vars.count("mood") && b.vars["mood"].values.size() == 2);
    CHECK(b.nodes["pier.start"].steps.size() == 10);
    CHECK(b.nodes["pier.start"].steps[4].text == "You are early. The tide is not.");
    // trace equivalence with the JS engine on the fixture
    auto src = std::vector<Source>{{"harbour.lwd", slurp(DIR + "harbour.lwd")}};
    for (int i = 1; i <= 6; i++) {
        std::string g = slurp(DIR + "harbour-s0" + std::to_string(i) + ".jsonl");
        auto r = raw::game::lowwater::replay(g, src, "");
        CHECK(r.error.empty()); CHECK(raw::game::lowwater::first_difference(r.trace, g) == 0);
    }
    // a changed host answer is a divergence, not a pass
    std::string g = slurp(DIR + "harbour-s01.jsonl"); auto p = g.find("\"r\":"); g.replace(p, 4, "\"r\":0,\"x\":");
    CHECK(raw::game::lowwater::first_difference(raw::game::lowwater::replay(g, src, "").trace, slurp(DIR + "harbour-s01.jsonl")) != 0);
    return raw_test_summary();
}
