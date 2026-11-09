#include "raw/story/story.hpp"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace raw::story {

std::string dump_number(double v) {
    if (!std::isfinite(v)) return "null";
    if (v == 0) return "0";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    std::string s(buf, r.ptr);
    // JS writes 1e-7 and 1e+21; to_chars writes 1e-07 and 1e+21
    auto e = s.find('e');
    if (e != std::string::npos) {
        std::string m = s.substr(0, e), x = s.substr(e + 1);
        char sign = '+'; if (!x.empty() && (x[0] == '+' || x[0] == '-')) { sign = x[0]; x = x.substr(1); }
        while (x.size() > 1 && x[0] == '0') x = x.substr(1);
        s = m + "e" + sign + x;
    }
    return s;
}

std::string json_string(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break; case '\f': o += "\\f"; break; case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    return o + "\"";
}

std::string dump_json(const Json& j) {
    switch (j.k) {
        case Json::Null: return "null";
        case Json::Bool: return j.b ? "true" : "false";
        case Json::Num: return dump_number(j.n);
        case Json::Str: return json_string(j.s);
        case Json::Arr: { std::string o = "["; for (size_t i = 0; i < j.a.size(); i++) { if (i) o += ","; o += dump_json(j.a[i]); } return o + "]"; }
        case Json::Obj: { std::string o = "{"; for (size_t i = 0; i < j.o.size(); i++) { if (i) o += ","; o += json_string(j.o[i].first) + ":" + dump_json(j.o[i].second); } return o + "}"; }
    }
    return "null";
}

namespace {
struct P {
    const std::string& t; size_t i = 0;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m + " at " + std::to_string(i)); }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    }
    unsigned hex4() { if (i + 4 > t.size()) fail("short \\u"); unsigned v = (unsigned)std::stoul(t.substr(i, 4), nullptr, 16); i += 4; return v; }
    std::string str() {
        std::string o; i++;
        while (i < t.size() && t[i] != '"') {
            if (t[i] != '\\') { o += t[i++]; continue; }
            i++; char c = t[i++];
            switch (c) {
                case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': { unsigned cp = hex4(); if (cp >= 0xD800 && cp < 0xDC00 && t.compare(i, 2, "\\u") == 0) { i += 2; unsigned lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); } utf8(o, cp); break; }
                default: o += c;
            }
        }
        if (i >= t.size()) fail("unterminated string");
        i++; return o;
    }
    Json val() {
        ws(); if (i >= t.size()) fail("unexpected end");
        char c = t[i];
        if (c == '{') {
            Json j = Json::obj(); i++; ws();
            if (t[i] == '}') { i++; return j; }
            for (;;) { ws(); std::string k = str(); ws(); if (t[i++] != ':') fail("expected :"); j.o.emplace_back(k, val()); ws(); if (t[i] == ',') { i++; continue; } if (t[i] == '}') { i++; break; } fail("expected , or }"); }
            return j;
        }
        if (c == '[') {
            Json j = Json::arr(); i++; ws();
            if (t[i] == ']') { i++; return j; }
            for (;;) { j.a.push_back(val()); ws(); if (t[i] == ',') { i++; continue; } if (t[i] == ']') { i++; break; } fail("expected , or ]"); }
            return j;
        }
        if (c == '"') return Json::str(str());
        if (t.compare(i, 4, "true") == 0) { i += 4; return Json::boolean(true); }
        if (t.compare(i, 5, "false") == 0) { i += 5; return Json::boolean(false); }
        if (t.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        size_t j = i; while (j < t.size() && (std::isdigit((unsigned char)t[j]) || t[j] == '-' || t[j] == '+' || t[j] == '.' || t[j] == 'e' || t[j] == 'E')) j++;
        if (j == i) fail("bad value");
        double d = 0; auto r = std::from_chars(t.data() + i, t.data() + j, d); if (r.ec != std::errc()) fail("bad number");
        i = j; return Json::num(d);
    }
};
}  // namespace

Json parse_json(const std::string& text) { P p{text}; Json j = p.val(); return j; }

// hashStr runs over UTF-16 code units, as charCodeAt does
uint32_t hash_str(const std::string& s, uint32_t seed) {
    uint32_t h = seed;
    auto mix = [&](unsigned unit) { h ^= unit; h *= 0x01000193u; };
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i]; unsigned cp; int n;
        if (c < 0x80) { cp = c; n = 1; } else if (c < 0xE0) { cp = c & 31; n = 2; } else if (c < 0xF0) { cp = c & 15; n = 3; } else { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 63);
        i += n;
        if (cp >= 0x10000) { cp -= 0x10000; mix(0xD800 + (cp >> 10)); mix(0xDC00 + (cp & 0x3FF)); } else mix(cp);
    }
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
double rand01(uint32_t seed, const std::string& key) { return (double)hash_str(key, seed) / 4294967296.0; }

}  // namespace raw::story
