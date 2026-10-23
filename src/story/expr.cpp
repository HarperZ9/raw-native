// Conditions and effects: a port of game/engine/core/expr.mjs in Low Water, same grammar and the
// same evaluation order (the order matters: it decides the order of host calls in a trace).
#include "raw/story/story.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace raw::story {

static int g_pivot = 12;
void set_clock_pivot(int h) { g_pivot = h; }
int clock_pivot() { return g_pivot; }
int pivot_for(int h) { return (h >= 17 || h < 4) ? 12 : (h + 18) % 24; }
double to_min(const std::string& s) {
    auto c = s.find(':'); int h = std::atoi(s.substr(0, c).c_str()), m = std::atoi(s.substr(c + 1).c_str());
    return (h < g_pivot ? h + 24 : h) * 60 + m;
}

bool truthy(const Val& v) {
    switch (v.k) { case Val::U: return false; case Val::B: return v.b; case Val::N: return v.n != 0 && !std::isnan(v.n); case Val::S: return !v.s.empty(); }
    return false;
}
static double str_num(const std::string& s) {   // Number(s) with NaN for junk
    size_t a = 0, b = s.size(); while (a < b && std::isspace((unsigned char)s[a])) a++; while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    if (a == b) return 0;
    std::string t = s.substr(a, b - a); char* e = nullptr; double d = std::strtod(t.c_str(), &e);
    return (e && *e == 0) ? d : std::nan("");
}
double to_num(const Val& v) {
    switch (v.k) { case Val::N: return v.n; case Val::B: return v.b ? 1 : 0; case Val::U: return 0;
        case Val::S: { double d = str_num(v.s); return std::isnan(d) ? 0 : d; } }
    return 0;
}
Val from_json(const Json& j) {
    switch (j.k) { case Json::Bool: return Val::boolean(j.b); case Json::Num: return Val::num(j.n); case Json::Str: return Val::str(j.s); default: return Val::undef(); }
}
Json to_json(const Val& v) {
    switch (v.k) { case Val::B: return Json::boolean(v.b); case Val::N: return std::isfinite(v.n) ? Json::num(v.n) : Json(); case Val::S: return Json::str(v.s); default: return Json(); }
}

// ---------------------------------------------------------------- tokens
namespace {
struct Tok { enum K { Time, Num, Str, Id, Op } k; double n = 0; std::string s; };
[[noreturn]] void bad(const std::string& m, const std::string& src) { throw std::runtime_error(m + " in: " + src); }

std::vector<Tok> tokenize(const std::string& src) {
    std::vector<Tok> out; size_t i = 0, n = src.size();
    while (true) {
        while (i < n && std::isspace((unsigned char)src[i])) i++;
        if (i >= n) break;
        unsigned char c = (unsigned char)src[i];
        if (std::isdigit(c)) {
            size_t j = i; while (j < n && std::isdigit((unsigned char)src[j])) j++;
            if (j + 2 < n && src[j] == ':' && std::isdigit((unsigned char)src[j + 1]) && std::isdigit((unsigned char)src[j + 2])) {
                Tok t{Tok::Time}; t.n = to_min(src.substr(i, j + 3 - i)); out.push_back(t); i = j + 3; continue;
            }
            if (j < n && src[j] == '.' && j + 1 < n && std::isdigit((unsigned char)src[j + 1])) { j++; while (j < n && std::isdigit((unsigned char)src[j])) j++; }
            Tok t{Tok::Num}; t.n = std::atof(src.substr(i, j - i).c_str()); out.push_back(t); i = j; continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1; std::string v;
            while (j < n && src[j] != (char)c) { if (src[j] == '\\' && j + 1 < n) j++; v += src[j++]; }
            if (j >= n) bad("unexpected \"" + src.substr(i, 12) + "\"", src);
            Tok t{Tok::Str}; t.s = v; out.push_back(t); i = j + 1; continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t j = i; while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '_' || src[j] == '.')) j++;
            std::string w = src.substr(i, j - i); i = j;
            Tok t{w == "and" || w == "or" || w == "not" ? Tok::Op : Tok::Id}; t.s = w == "and" ? "&" : w == "or" ? "|" : w == "not" ? "!" : w;
            out.push_back(t); continue;
        }
        static const char* two[] = {"||", "&&", "<=", ">=", "==", "!=", "+=", "-=", "*="};
        bool done = false;
        for (auto* p : two) if (src.compare(i, 2, p) == 0) { Tok t{Tok::Op}; t.s = p; out.push_back(t); i += 2; done = true; break; }
        if (done) continue;
        if (std::string("-+*/%<>=!&|(),;").find((char)c) != std::string::npos) { Tok t{Tok::Op}; t.s = std::string(1, (char)c); out.push_back(t); i++; continue; }
        bad("unexpected \"" + src.substr(i, 12) + "\"", src);
    }
    return out;
}

NodeP mk(const char* k) { auto n = std::make_shared<Node>(); n->k = k; return n; }

struct Parser {
    std::vector<Tok> t; std::string src; size_t p = 0;
    std::string norm(const std::string& s) const { return s == "||" ? "|" : s == "&&" ? "&" : s; }
    bool is_op(const char* v) const { return p < t.size() && t[p].k == Tok::Op && norm(t[p].s) == v; }
    void expect(const char* v) { if (!is_op(v)) bad(std::string("expected \"") + v + "\"", src); p++; }
    NodeP or_() { auto a = and_(); while (is_op("|")) { p++; auto n = mk("or"); n->a = a; n->b = and_(); a = n; } return a; }
    NodeP and_() { auto a = not_(); while (is_op("&")) { p++; auto n = mk("and"); n->a = a; n->b = not_(); a = n; } return a; }
    NodeP not_() { if (is_op("!")) { p++; auto n = mk("not"); n->a = not_(); return n; } return cmp(); }
    NodeP cmp() {
        auto a = sum();
        for (const char* op : {"<=", ">=", "==", "!=", "<", ">"}) if (is_op(op)) { p++; auto n = mk("cmp"); n->op = op; n->a = a; n->b = sum(); return n; }
        if (is_op("=") && p + 1 < t.size() && t[p + 1].k != Tok::Op) { p++; auto n = mk("cmp"); n->op = "=="; n->a = a; n->b = sum(); return n; }
        return a;
    }
    NodeP sum() { auto a = prod(); while (is_op("+") || is_op("-")) { auto n = mk("bin"); n->op = t[p++].s; n->a = a; n->b = prod(); a = n; } return a; }
    NodeP prod() { auto a = unary(); while (is_op("*") || is_op("/") || is_op("%")) { auto n = mk("bin"); n->op = t[p++].s; n->a = a; n->b = unary(); a = n; } return a; }
    NodeP unary() { if (is_op("-")) { p++; auto n = mk("neg"); n->a = unary(); return n; } return primary(); }
    NodeP primary() {
        if (p >= t.size()) bad("unexpected end", src);
        const Tok& tk = t[p];
        if (tk.k == Tok::Num || tk.k == Tok::Time) { p++; auto n = mk("lit"); n->lit = Val::num(tk.n); return n; }
        if (tk.k == Tok::Str) { p++; auto n = mk("lit"); n->lit = Val::str(tk.s); return n; }
        if (is_op("(")) { p++; auto e = or_(); expect(")"); return e; }
        if (tk.k == Tok::Id) {
            p++;
            if (tk.s == "true" || tk.s == "false") { auto n = mk("lit"); n->lit = Val::boolean(tk.s == "true"); return n; }
            if (is_op("(")) {
                p++; auto n = mk("call"); n->name = tk.s;
                if (!is_op(")")) { do { n->args.push_back(or_()); } while (is_op(",") && ++p); }
                expect(")"); return n;
            }
            auto n = mk("id"); n->name = tk.s; return n;
        }
        bad("unexpected \"" + tk.s + "\"", src);
    }
};
}  // namespace

ExprP compile(const std::string& src) {
    size_t a = src.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return nullptr;
    Parser P{tokenize(src), src}; auto ast = P.or_();
    if (P.p < P.t.size()) bad("unexpected \"" + P.t[P.p].s + "\"", src);
    return std::make_shared<Expr>(Expr{src, ast});
}

ExprP compile_effects(const std::string& src) {
    if (src.find_first_not_of(" \t\r\n") == std::string::npos) return nullptr;
    Parser P{tokenize(src), src}; auto root = mk("effects");
    while (P.p < P.t.size()) {
        if (P.is_op(";")) { P.p++; continue; }
        const Tok& t = P.t[P.p];
        if (t.k == Tok::Id && P.p + 1 < P.t.size() && P.t[P.p + 1].k == Tok::Op && (P.t[P.p + 1].s == "=" || P.t[P.p + 1].s == "+=" || P.t[P.p + 1].s == "-=" || P.t[P.p + 1].s == "*=")) {
            auto n = mk("assign"); n->name = t.s; n->op = P.t[P.p + 1].s; P.p += 2; n->a = P.or_(); root->list.push_back(n);
        } else {
            auto e = P.or_(); if (e->k != "call") bad("an effect is an assignment or a call", src);
            root->list.push_back(e);
        }
        if (P.p < P.t.size() && !P.is_op(";")) bad("expected \";\" before \"" + P.t[P.p].s + "\"", src);
    }
    return std::make_shared<Expr>(Expr{src, root});
}

// ---------------------------------------------------------------- evaluation
static bool loose_eq(const Val& a, const Val& b) {
    if (a.k == Val::U || b.k == Val::U) return a.k == b.k;
    if (a.k == b.k) return a.k == Val::S ? a.s == b.s : a.k == Val::N ? a.n == b.n : a.b == b.b;
    auto nm = [](const Val& v) { return v.k == Val::S ? str_num(v.s) : to_num(v); };
    return nm(a) == nm(b);
}

Val Scope::arg_of(const NodeP& a) { return a->k == "id" ? Val::str(a->name) : eval(a); }
Val Scope::value_of(const NodeP& a) { Val v = eval(a); return (v.k == Val::U && a->k == "id") ? Val::str(a->name) : v; }

Val Scope::eval(const NodeP& n) {
    const std::string& k = n->k;
    if (k == "lit") return n->lit;
    if (k == "id") return get(n->name);
    if (k == "not") return Val::boolean(!truthy(eval(n->a)));
    if (k == "and") return Val::boolean(truthy(eval(n->a)) && truthy(eval(n->b)));
    if (k == "or") return Val::boolean(truthy(eval(n->a)) || truthy(eval(n->b)));
    if (k == "neg") return Val::num(-to_num(eval(n->a)));
    if (k == "bin") {
        double a = to_num(eval(n->a)), b = to_num(eval(n->b));
        const std::string& o = n->op;
        return Val::num(o == "+" ? a + b : o == "-" ? a - b : o == "*" ? a * b : o == "/" ? (b != 0 ? a / b : 0) : (b != 0 ? std::fmod(a, b) : 0));
    }
    if (k == "cmp") {
        auto word = [&](const NodeP& x, const NodeP& y) { return x->k == "id" && y->k == "id" && is_word(x->name, y->name); };
        Val a = word(n->b, n->a) ? Val::str(n->a->name) : eval(n->a);
        Val b = word(n->a, n->b) ? Val::str(n->b->name) : eval(n->b);
        if (a.k == Val::U && n->a->k == "id") a = Val::str(n->a->name);
        if (b.k == Val::U && n->b->k == "id") b = Val::str(n->b->name);
        if (a.k == Val::B && b.k == Val::N) a = Val::num(a.b);
        if (b.k == Val::B && a.k == Val::N) b = Val::num(b.b);
        const std::string& o = n->op;
        if (o == "==") return Val::boolean(loose_eq(a, b));
        if (o == "!=") return Val::boolean(!loose_eq(a, b));
        double x = to_num(a), y = to_num(b);
        return Val::boolean(o == "<" ? x < y : o == "<=" ? x <= y : o == ">" ? x > y : x >= y);
    }
    if (k == "call") return call(n->name, n->args);
    throw std::runtime_error("expr: unknown node " + k);
}

bool evaluate(const ExprP& c, Scope& S) { return !c || truthy(S.eval(c->ast)); }

void run_effects(const ExprP& c, Scope& S) {
    if (!c) return;
    for (auto& s : c->ast->list) {
        if (s->k == "call") { S.call(s->name, s->args); continue; }
        if (s->op == "=" && s->a->k == "id" && S.is_word(s->name, s->a->name)) { S.assign(s->name, Val::str(s->a->name)); continue; }
        Val v = S.eval(s->a); double cur = to_num(S.get(s->name));
        if (s->op == "=") S.assign(s->name, (v.k == Val::U && s->a->k == "id") ? Val::str(s->a->name) : v);
        else if (s->op == "+=") S.assign(s->name, Val::num(cur + to_num(v)));
        else if (s->op == "-=") S.assign(s->name, Val::num(cur - to_num(v)));
        else S.assign(s->name, Val::num(cur * to_num(v)));
    }
}

}  // namespace raw::story
