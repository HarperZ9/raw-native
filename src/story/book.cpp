// LWD text to records and steps: a port of dialogue/parse.mjs and dialogue/book.mjs in Low Water.
#include "raw/story/story.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace raw::story {
namespace {

std::string trim_end(std::string s) { while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); return s; }
std::string trim_start(const std::string& s) { size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) a++; return s.substr(a); }
std::string trim(const std::string& s) { return trim_end(trim_start(s)); }

std::vector<std::string> split_lines(const std::string& t) {
    std::vector<std::string> out; std::string cur;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '\n') { out.push_back(cur); cur.clear(); }
        else if (t[i] == '\r' && i + 1 < t.size() && t[i + 1] == '\n') {}
        else cur += t[i];
    }
    out.push_back(cur); return out;
}

std::vector<Record> parse_records(const std::string& text, const std::string& file, std::vector<std::string>& errors) {
    std::vector<Record> recs; Record* cur = nullptr; int n = 0;
    for (auto& raw : split_lines(text)) {
        n++; std::string line = trim_end(raw);
        if (trim(line).empty() || trim_start(line)[0] == '#') continue;
        if (line[0] == '@') {
            std::string rest = trim(line.substr(1)); Record r; r.line = n;
            size_t sp = rest.find_first_of(" \t"); r.kind = rest.substr(0, sp);
            if (sp != std::string::npos) { size_t b = rest.find_first_not_of(" \t", sp); if (b != std::string::npos) { size_t e = rest.find_first_of(" \t", b); r.id = rest.substr(b, e == std::string::npos ? e : e - b); } }
            recs.push_back(r); cur = &recs.back(); continue;
        }
        if (!cur) { errors.push_back(file + ":" + std::to_string(n) + " content before any record"); continue; }
        size_t k = line.find(':');
        if (k == std::string::npos) { errors.push_back(file + ":" + std::to_string(n) + " expected \"key: value\""); continue; }
        std::string v = line.substr(k + 1); if (!v.empty() && v[0] == ' ') v = v.substr(1);
        cur->kv.emplace_back(trim(line.substr(0, k)), v);
    }
    return recs;
}

// `... {once, dc: 12, do: xp(5); rep(guild, 2)}` at the end of a value
Attrs parse_attrs(const std::string& src) {
    Attrs out; std::vector<std::string> parts; int depth = 0; char q = 0; std::string cur;
    for (char ch : src) {
        if (q) { cur += ch; if (ch == q) q = 0; continue; }
        if (ch == '"' || ch == '\'') q = ch;
        if (ch == '(') depth++; else if (ch == ')') depth--;
        if (ch == ',' && depth == 0) { parts.push_back(cur); cur.clear(); } else cur += ch;
    }
    if (!trim(cur).empty()) parts.push_back(cur);
    static const std::regex NUM("^-?\\d+(\\.\\d+)?$");
    for (auto& p : parts) {
        size_t k = p.find(':'); Attr a;
        if (k == std::string::npos) { a.k = Attr::Bool; a.b = true; out[trim(p)] = a; continue; }
        std::string v = trim(p.substr(k + 1));
        if (std::regex_match(v, NUM)) { a.k = Attr::Num; a.n = std::atof(v.c_str()); } else { a.k = Attr::Str; a.s = v; }
        out[trim(p.substr(0, k))] = a;
    }
    return out;
}
std::string split_attrs(const std::string& v, Attrs& attrs) {
    size_t e = v.size(); while (e > 0 && std::isspace((unsigned char)v[e - 1])) e--;
    if (e == 0 || v[e - 1] != '}') return v;
    size_t close = e - 1, open = std::string::npos;
    for (size_t i = close; i-- > 0;) { if (v[i] == '}') return v; if (v[i] == '{') { open = i; break; } }
    if (open == std::string::npos) return v;
    attrs = parse_attrs(v.substr(open + 1, close - open - 1));
    size_t b = open; while (b > 0 && std::isspace((unsigned char)v[b - 1])) b--;
    return v.substr(0, b);
}

// direction.mjs: only what decides which text the screen shows
bool direction_block_valid(const std::string& src) {
    static const std::set<std::string> EMO = {"flat","calm","dry","warm","tender","wry","amused","ecstatic","proud","cold","stern","angry","contempt","afraid","anxious","pleading","grieving","tired","ashamed","drunk"};
    static const std::set<std::string> DELIVERY = {"whisper","low","raised","shout","sung"}, PACE = {"slow","steady","fast"}, FLAGS = {"lie","aside","over","cut","held"};
    std::vector<std::string> toks; std::string cur;
    for (char c : src) { if (std::isspace((unsigned char)c) || c == ',') { if (!cur.empty()) toks.push_back(cur), cur.clear(); } else cur += c; }
    if (!cur.empty()) toks.push_back(cur);
    int errors = 0; std::map<std::string, std::string> dir; bool any = false;
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    auto isnum = [](const std::string& s) { static const std::regex R("^-?\\d+(\\.\\d+)?$"); return std::regex_match(s, R); };
    for (size_t i = 0; i < toks.size(); i++) {
        std::string t = lower(toks[i]);
        if (!t.empty() && t.back() == ':') { std::string k = t.substr(0, t.size() - 1), v = i + 1 < toks.size() ? lower(toks[++i]) : ""; t = k + ":" + v; }
        std::string k, v; size_t c = t.find(':'); bool has = c != std::string::npos; if (has) { k = t.substr(0, c); v = t.substr(c + 1); } else v = t;
        auto set = [&](const std::string& key, const std::string& val) { if (dir.count(key) && dir[key] != val) errors++; dir[key] = val; any = true; };
        if (k == "emo" || (!has && EMO.count(v))) { if (!EMO.count(v)) errors++; else set("emo", v); }
        else if (k == "int" || (!has && (v == "1" || v == "2" || v == "3"))) { if (!(v == "1" || v == "2" || v == "3")) errors++; else set("int", v); }
        else if (k == "pace") { if (PACE.count(v)) set("pace", v); else if (isnum(v) && std::atof(v.c_str()) >= 0.5 && std::atof(v.c_str()) <= 1.6) set("pace", v); else errors++; }
        else if (!has && PACE.count(v)) set("pace", v);
        else if (k == "vol" || (!has && DELIVERY.count(v))) { if (!DELIVERY.count(v)) errors++; else set("vol", v); }
        else if (!has && FLAGS.count(v)) { dir[v] = "1"; any = true; }
        else errors++;
    }
    return any || errors == 0;
}

// splitDirected(text).text: the words the screen shows
std::string shown_text(const std::string& raw) {
    std::string text = raw;
    size_t a = 0; while (a < text.size() && std::isspace((unsigned char)text[a])) a++;
    if (a < text.size() && text[a] == '[') {
        size_t c = text.find(']', a);
        if (c != std::string::npos && direction_block_valid(text.substr(a + 1, c - a - 1))) {
            size_t e = c + 1; while (e < text.size() && std::isspace((unsigned char)text[e])) e++;
            text = text.substr(e);
        }
    }
    static const std::regex CUE("<(\\w+)(?:\\s+(\\d+(?:\\.\\d+)?))?>");
    text = std::regex_replace(text, CUE, " ");
    std::string o; for (char c : text) if (c != '*') o += c;
    static const std::regex MULTI("\\s{2,}"), PUNCT("\\s+([.,;:!?])");
    o = std::regex_replace(o, MULTI, " "); o = std::regex_replace(o, PUNCT, "$1");
    return trim(o);
}

void speaker_of(const std::string& v, std::string& who, std::string& text) {
    size_t k = v.find(": ");
    if (k == std::string::npos) { who.clear(); text = v; } else { who = trim(v.substr(0, k)); text = v.substr(k + 2); }
}

const char* TARGET = "[\\w.^]+";

ExprP cond_of(const Attrs& at, const char* key) {
    auto it = at.find(key); if (it == at.end() || it->second.k != Attr::Str) return nullptr;
    return compile(it->second.s);
}

Step parse_choice(const std::string& kind, const std::string& value, int line) {
    Step s; s.kind = kind; s.line = line;
    std::string body = trim(split_attrs(value, s.attrs));
    static const std::regex OPT(std::string("^\"([^\"]*)\"\\s*->\\s*(") + TARGET + ")(?:\\s*\\/\\s*(" + TARGET + "))?(?:\\s*\\?\\s*(.*?))?(?:\\s+\\+(\\d+))?\\s*$");
    std::smatch m;
    if (!std::regex_match(body, m, OPT)) throw std::runtime_error(kind + ": expected \"label\" -> target, got " + value);
    s.label = m[1]; s.target = m[2]; s.fail = m[3].matched ? m[3].str() : "";
    if (m[4].matched) s.cond = compile(trim(m[4].str()));
    s.cost = m[5].matched ? std::atoi(m[5].str().c_str()) : 0;
    if (s.attrs.count("do") && s.attrs["do"].k == Attr::Str) s.effects = compile_effects(s.attrs["do"].s);
    std::vector<std::string> show;
    if (s.attrs.count("show") && s.attrs["show"].k == Attr::Str && !trim(s.attrs["show"].s).empty()) show.push_back(s.attrs["show"].s);
    if (kind == "claim" && s.attrs.count("when") && s.attrs["when"].k == Attr::Str && !trim(s.attrs["when"].s).empty()) show.push_back(s.attrs["when"].s);
    if (!show.empty()) { std::string j; for (size_t i = 0; i < show.size(); i++) j += (i ? " & (" : "(") + show[i] + ")"; s.show = compile(j); }
    if (s.attrs.count("when") && kind != "claim") {
        const std::string& w = s.attrs["when"].s;
        s.cond = s.cond ? compile("(" + s.cond->src + ") & (" + w + ")") : compile(w);
    }
    if (kind == "check") {
        if (!s.attrs.count("skill")) throw std::runtime_error("check: needs {skill: name, dc: n}: " + value);
        if (s.fail.empty()) throw std::runtime_error("check: needs \"-> pass / fail\": " + value);
    }
    if (kind == "claim" && s.fail.empty()) throw std::runtime_error("claim: needs \"-> ok / fail\": " + value);
    return s;
}

Step parse_step(const std::string& key, const std::string& value, int line) {
    std::string v = trim(value); Step s; s.kind = key; s.line = line;
    if (key == "opt" || key == "claim" || key == "check") return parse_choice(key, value, line);
    if (key == "alt") {
        static const std::regex R(std::string("^(") + TARGET + ")\\s*\\?\\s*(.*)$"); std::smatch m;
        if (!std::regex_match(v, m, R)) throw std::runtime_error("alt: expected \"target ? cond\", got " + v);
        s.target = m[1]; s.cond = compile(m[2]); return s;
    }
    if (key == "when") { s.cond = compile(v); return s; }
    if (key == "do") { s.effects = compile_effects(v); return s; }
    if (key == "if") {
        size_t k = v.find("=>"); if (k == std::string::npos) throw std::runtime_error("if: expected \"cond => effects\", got " + v);
        s.cond = compile(v.substr(0, k)); s.effects = compile_effects(v.substr(k + 2)); return s;
    }
    if (key == "line" || key == "inst") {
        std::string body = split_attrs(value, s.attrs), t; speaker_of(body, s.who, t); s.text = shown_text(t);
        s.cond = cond_of(s.attrs, "when"); s.once = s.attrs.count("once") && s.attrs["once"].truthy(); return s;
    }
    if (key == "nar") {
        std::string body = split_attrs(value, s.attrs); s.value = shown_text(trim(body));
        s.cond = cond_of(s.attrs, "when"); s.once = s.attrs.count("once") && s.attrs["once"].truthy(); return s;
    }
    if (key == "voice") {
        std::string body = split_attrs(value, s.attrs), t; speaker_of(body, s.skill, t); s.text = shown_text(t);
        if (s.attrs.count("dc")) { s.has_dc = true; s.dc = s.attrs["dc"].n; }
        if (s.attrs.count("below")) { s.has_below = true; s.below = s.attrs["below"].n; }
        s.cond = cond_of(s.attrs, "when"); s.once = s.attrs.count("once") && s.attrs["once"].truthy(); return s;
    }
    if (key == "passive") {
        std::string body = trim(split_attrs(value, s.attrs));
        static const std::regex R(std::string("^([\\w.]+)\\s*->\\s*(") + TARGET + ")$"); std::smatch m;
        if (!std::regex_match(body, m, R)) throw std::runtime_error("passive: expected \"skill -> node {dc: n}\", got " + v);
        s.skill = m[1]; s.target = m[2];
        if (s.attrs.count("dc")) { s.has_dc = true; s.dc = s.attrs["dc"].n; }
        s.cond = cond_of(s.attrs, "when");
        s.once = !(s.attrs.count("once") && s.attrs["once"].k == Attr::Bool && !s.attrs["once"].b);   // attrs.once !== false
        if (s.attrs.count("once") && s.attrs["once"].k != Attr::Bool) s.once = true;
        return s;
    }
    if (key == "dir") { speaker_of(v, s.who, s.text); return s; }
    if (key == "set" || key == "clear") { s.value = v; s.effects = compile_effects(key + "(" + v + ")"); return s; }
    if (key == "xp") { s.kind = "do"; s.effects = compile_effects("xp(" + v + ")"); return s; }
    if (key == "cost") { s.dc = std::atof(v.c_str()); s.value = v; return s; }
    if (key == "cmd") { std::string cur; for (char c : v) { if (std::isspace((unsigned char)c)) { if (!cur.empty()) s.args.push_back(cur), cur.clear(); } else cur += c; } if (!cur.empty()) s.args.push_back(cur); return s; }
    if (key == "return") return s;
    static const std::set<std::string> PLAIN = {"go","call","end","exit","place","read","else","speaker","hub","title","note","camera","walk"};
    s.value = v; if (!PLAIN.count(key)) s.kind = "unknown"; return s;
}

}  // namespace

Book load_book(const std::vector<Source>& sources) {
    Book book;
    // the clock pivot is known before any time is parsed
    int start_hour = 23;
    for (auto& src : sources) {
        bool in_meta = false;
        for (auto& l : split_lines(src.text)) {
            if (l.rfind("@meta", 0) == 0) { in_meta = true; continue; }
            if (!l.empty() && l[0] == '@') in_meta = false;
            if (in_meta && l.rfind("clock:", 0) == 0) { start_hour = std::atoi(trim(l.substr(6)).c_str()); goto found; }
        }
    }
found:
    set_clock_pivot(pivot_for(start_hour));
    for (auto& src : sources) {
        std::vector<std::string> errs; auto recs = parse_records(src.text, src.file, errs);
        for (auto& e : errs) book.errors.push_back(e);
        for (auto& r : recs) {
            book.records++;
            if (r.kind == "meta") { for (auto& [k, v] : r.kv) { if (k == "include") book.includes.push_back(trim(v)); else book.meta[k] = v; } continue; }
            if (r.kind == "var") { VarDecl d; std::string def = r.one("default"); bool has = false; for (auto& p : r.kv) if (p.first == "default") has = true;
                if (!has) d.def = Val::num(0); else { char* e = nullptr; double x = std::strtod(def.c_str(), &e); if (!def.empty() && e && *e == 0 && def.find_first_not_of(" ") != std::string::npos) d.def = Val::num(x); else if (def == "true") d.def = Val::boolean(true); else if (def == "false") d.def = Val::boolean(false); else d.def = Val::str(trim(def)); }
                std::string vals = r.one("values"); std::string cur; for (char c : vals + ",") { if (c == ',') { std::string t = trim(cur); if (!t.empty()) d.values.push_back(t); cur.clear(); } else cur += c; }
                book.vars[r.id] = d; continue; }
            if (r.kind == "skill") { book.skills[r.id] = r; continue; }
            if (r.kind == "fact") { if (!book.facts.count(r.id)) book.facts[r.id] = r; continue; }
            if (r.kind != "node" && r.kind != "event") continue;
            // steps: keys in order, each with its source line
            auto lines = split_lines(src.text);
            std::vector<Step> steps; int choice = 0;
            int li = r.line;   // 1-based line of the @record; walk its body lines in order
            std::vector<std::pair<std::string, std::pair<std::string, int>>> body;
            for (int n = li + 1; n <= (int)lines.size(); n++) {
                std::string line = trim_end(lines[n - 1]);
                if (trim(line).empty() || trim_start(line)[0] == '#') continue;
                if (line[0] == '@') break;
                size_t k = line.find(':'); if (k == std::string::npos) continue;
                std::string v = line.substr(k + 1); if (!v.empty() && v[0] == ' ') v = v.substr(1);
                body.push_back({trim(line.substr(0, k)), {v, n}});
            }
            ExprP when; double at = -1e300;
            for (auto& [key, vl] : body) {
                if (r.kind == "event" && (key == "at" || key == "when")) {
                    if (key == "at") at = to_min(trim(vl.first)); else { try { when = compile(vl.first); } catch (std::exception& e) { book.errors.push_back(e.what()); } }
                    continue;
                }
                try {
                    Step s = parse_step(key, vl.first, vl.second);
                    if (s.kind == "opt" || s.kind == "claim" || s.kind == "check") s.key = s.attrs.count("id") ? s.attrs["id"].s : r.id + "#" + std::to_string(choice++);
                    if (s.kind == "voice" || s.kind == "passive" || s.once) s.key = r.id + "@" + std::to_string(vl.second);
                    steps.push_back(std::move(s));
                } catch (std::exception& e) { book.errors.push_back(src.file + ":" + std::to_string(vl.second) + " " + e.what()); }
            }
            if (r.kind == "event") { EventDef e; e.id = r.id; e.at = at; e.when = when; e.steps = steps; book.events.push_back(e); continue; }
            if (book.nodes.count(r.id)) { book.errors.push_back("duplicate node " + r.id); continue; }
            NodeDef nd; nd.id = r.id; nd.steps = steps;
            for (auto& s : steps) {
                if (s.kind == "hub") nd.hub = s.value == "yes" || s.value == "true";
                if (s.kind == "speaker") nd.speaker = s.value;
            }
            book.nodes[r.id] = nd; book.node_order.push_back(r.id);
        }
    }
    if (book.meta.count("clock")) book.clock0 = to_min(trim(book.meta["clock"]));
    auto endc = book.meta.find("end-clock"), atend = book.meta.find("at-end");
    if (endc != book.meta.end() && atend != book.meta.end()) {
        bool have = false; for (auto& e : book.events) if (e.id == "meta.at-end") have = true;
        if (!have) { EventDef e; e.id = "meta.at-end"; e.at = to_min(trim(endc->second)); Step g; g.kind = "go"; g.value = trim(atend->second); e.steps.push_back(g); book.events.push_back(e); }
    }
    for (auto& e : book.events) { NodeDef nd; nd.id = "@event." + e.id; nd.steps = e.steps; nd.event = true; book.nodes[nd.id] = nd; book.node_order.push_back(nd.id); }
    return book;
}

std::vector<Source> load_act_sources(const std::string& root_path, std::string (*read)(const std::string&)) {
    std::string dir = root_path.substr(0, root_path.find_last_of("/\\") + 1), root = read(root_path);
    std::vector<Source> out{{root_path, root}}; std::vector<std::string> errs;
    for (auto& r : parse_records(root, root_path, errs)) if (r.kind == "meta")
        for (auto& [k, v] : r.kv) if (k == "include") out.push_back({dir + trim(v), read(dir + trim(v))});
    return out;
}

}  // namespace raw::story
