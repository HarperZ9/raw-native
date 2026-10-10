#include "raw/assets/json.hpp"
#include <cmath>
#include <cstdlib>
namespace raw::assets {
namespace {
constexpr int kMaxDepth = 64;
constexpr std::size_t kMaxInput = std::size_t(256) << 20;

struct Parser {
    std::string_view t;
    std::size_t i = 0;
    [[noreturn]] void fail(const char* m) const { throw AssetError(std::string("json: ") + m + " at byte " + std::to_string(i)); }
    void ws(){ while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    bool eat(char c){ ws(); if (i < t.size() && t[i] == c){ ++i; return true; } return false; }
    void expect(char c){ if (!eat(c)) fail("unexpected character"); }
    void literal(std::string_view w){ if (t.substr(i, w.size()) != w) fail("bad literal"); i += w.size(); }

    static void utf8(std::string& out, std::uint32_t cp){
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800){ out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000){ out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    std::uint32_t hex4(){
        if (i + 4 > t.size()) fail("short \\u escape");
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k){
            const char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }
    std::string str(){
        ws();
        if (i >= t.size() || t[i] != '"') fail("expected a string");
        ++i;
        std::string out;
        for (;;){
            if (i >= t.size()) fail("unterminated string");
            const char c = t[i++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\'){ out += c; continue; }
            if (i >= t.size()) fail("unterminated escape");
            const char e = t[i++];
            switch (e){
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && t.substr(i, 2) == "\\u"){
                    i += 2;
                    const std::uint32_t lo = hex4();
                    if (lo < 0xDC00 || lo > 0xDFFF) fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
    }
    double num(){
        const std::size_t s = i;
        if (i < t.size() && t[i] == '-') ++i;
        auto digits = [&]{ const std::size_t d = i; while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i; return i > d; };
        if (!digits()) fail("bad number");
        if (i < t.size() && t[i] == '.'){ ++i; if (!digits()) fail("bad number"); }
        if (i < t.size() && (t[i] == 'e' || t[i] == 'E')){ ++i; if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i; if (!digits()) fail("bad number"); }
        const std::string buf(t.substr(s, i - s));
        const double v = std::strtod(buf.c_str(), nullptr);
        if (!std::isfinite(v)) fail("number out of range");
        return v;
    }
    Json value(int depth){
        if (depth > kMaxDepth) fail("nested too deeply");
        ws();
        if (i >= t.size()) fail("unexpected end");
        Json j;
        const char c = t[i];
        if (c == '{'){
            ++i; j.kind = Json::Kind::Object;
            if (eat('}')) return j;
            do {
                std::string k = str();
                expect(':');
                j.o.insert_or_assign(std::move(k), value(depth + 1));
            } while (eat(','));
            expect('}');
        } else if (c == '['){
            ++i; j.kind = Json::Kind::Array;
            if (eat(']')) return j;
            do { j.a.push_back(value(depth + 1)); } while (eat(','));
            expect(']');
        } else if (c == '"'){ j.kind = Json::Kind::String; j.s = str(); }
        else if (c == 't'){ literal("true"); j.kind = Json::Kind::Bool; j.b = true; }
        else if (c == 'f'){ literal("false"); j.kind = Json::Kind::Bool; }
        else if (c == 'n'){ literal("null"); }
        else { j.kind = Json::Kind::Number; j.n = num(); }
        return j;
    }
};
}

const Json* Json::get(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    const auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}
const Json& Json::at(std::size_t idx) const {
    if (kind != Kind::Array || idx >= a.size()) throw AssetError("json: index " + std::to_string(idx) + " out of range");
    return a[idx];
}
double Json::number(const char* what) const {
    if (kind != Kind::Number) throw AssetError(std::string(what) + ": expected a number");
    return n;
}
std::int64_t Json::integer(const char* what, std::int64_t lo, std::int64_t hi) const {
    const double v = number(what);
    if (v != std::floor(v) || v < double(lo) || v > double(hi)) throw AssetError(std::string(what) + ": expected an integer from " + std::to_string(lo) + " to " + std::to_string(hi));
    return static_cast<std::int64_t>(v);
}

Json parseJson(std::string_view text){
    if (text.size() > kMaxInput) throw AssetError("json: input larger than 256 MiB");
    Parser p{text};
    Json j = p.value(0);
    p.ws();
    if (p.i != text.size()) p.fail("trailing characters");
    return j;
}

}
