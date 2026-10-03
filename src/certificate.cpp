#include "raw/certificate.hpp"
#include <cstdio>
#include <cmath>
#include <optional>
namespace raw {
// Emit a finite double as a JSON number, or the literal null for absent/non-finite.
// Honest witnessing: a channel with no data is null, never a fabricated value.
static std::string jnum(const std::optional<double>& v){
    if (!v.has_value() || !std::isfinite(*v)) return "null";
    char b[64]; std::snprintf(b, sizeof b, "%.6g", *v); return b;
}
const char* verdict_str(Verdict v){
    switch (v){
        case Verdict::Verified:     return "verified";
        case Verdict::Refuted:      return "refuted";
        case Verdict::Unverifiable: return "unverifiable";
    }
    return "unverifiable";
}
// Emit a JSON string literal (with surrounding quotes), escaping per RFC 8259.
static std::string jstr(const std::string& s){
    std::string o = "\"";
    for (unsigned char ch : s){
        switch (ch){
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (ch < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
                else o += static_cast<char>(ch);
        }
    }
    o += "\"";
    return o;
}
std::string to_json(const Certificate& c){
    std::string o = "{";
    o += "\"claim\":"   + jstr(c.claim)                 + ",";
    o += "\"verdict\":" + jstr(verdict_str(c.verdict))  + ",";
    o += "\"oracle\":"  + jstr(c.oracle)                + ",";
    o += "\"evidence\":[";
    for (std::size_t i = 0; i < c.evidence.size(); ++i){
        if (i) o += ",";
        o += "[" + jstr(c.evidence[i].first) + "," + jstr(c.evidence[i].second) + "]";
    }
    o += "]";
    // Additive: append the per-channel fidelity block only when present, so a
    // certificate without channels stays byte-identical to the original shape.
    if (c.channels.has_value()){
        const ChannelFidelity& ch = *c.channels;
        o += ",\"channels\":{";
        o += "\"ao_fidelity\":"      + jnum(ch.aoFidelity)      + ",";
        o += "\"motion_coherence\":" + jnum(ch.motionCoherence) + ",";
        o += "\"hdr_headroom\":"     + jnum(ch.hdrHeadroom);
        o += "}";
    }
    o += "}";
    return o;
}
}
