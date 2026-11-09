// Marker clips: see raw/game/action/markers.hpp.
#include "raw/game/action/markers.hpp"
#include <stdexcept>

namespace raw::game::action {
namespace {

int int_of(const Json& o, const char* k) {
    auto* p = o.get(k);
    if (!p || p->k != Json::Num) throw std::runtime_error(std::string("markers: missing ") + k);
    return (int)p->n;
}
void pair_of(const Json* p, double out[2]) {
    if (p && p->k == Json::Arr && p->a.size() >= 2) { out[0] = p->a[0].n; out[1] = p->a[1].n; }
}

MarkerClip clip_of(const std::string& key, const Json& j) {
    MarkerClip c; c.key = key;
    const Json& mk = *j.get("markers");
    c.active = int_of(mk, "active"); c.recovery = int_of(mk, "recovery"); c.end = int_of(mk, "end");
    if (auto* t = mk.get("tell")) { c.has_tell = true; c.tell_on = (int)t->a[0].n; c.tell_off = (int)t->a[1].n; }
    if (auto* sm = j.get("smear"); sm && sm->k == Json::Arr && sm->a.size() >= 2) { c.smear_from = (int)sm->a[0].n; c.smear_to = (int)sm->a[1].n; }
    for (const Json& h : j.get("hits")->a) {
        HitMarker m; m.on = int_of(h, "on"); m.off = int_of(h, "off");
        pair_of(h.get("arc"), m.arc); pair_of(h.get("r"), m.r); pair_of(h.get("y"), m.y);
        c.hits.push_back(m);
    }
    auto flag = [&](const char* k, bool d) { auto* p = j.get(k); return p && p->k == Json::Bool ? p->b : d; };
    c.tell_kind = j.str_or("tellKind");
    c.parryable = flag("parryable", true); c.blockable = flag("blockable", true); c.signature = flag("signature", false);
    if (!(0 <= c.active && c.active <= c.recovery && c.recovery <= c.end))
        throw std::runtime_error("markers: " + key + " runs backwards");
    return c;
}

void diff(std::vector<std::string>& out, const std::string& key, const std::string& what, int a, int b) {
    if (a != b) out.push_back(key + " " + what + " clip=" + std::to_string(a) + " move=" + std::to_string(b));
}

}  // namespace

raw::anim::Layout MarkerClip::phases() const {
    return raw::anim::layout({{"startup", (double)active}, {"active", (double)(recovery - active)}, {"recovery", (double)(end - recovery)}});
}

const MarkerClip* MarkerSet::find(const std::string& key) const {
    for (const MarkerClip& c : clips) if (c.key == key) return &c;
    return nullptr;
}

MarkerSet load_markers(const std::string& text) {
    const Json j = raw::story::parse_json(text);
    if (j.str_or("format") != "raw-clip-markers") throw std::runtime_error("markers: not a raw-clip-markers file");
    if (!j.get("version") || j.get("version")->n != 1) throw std::runtime_error("markers: version must be 1");
    if (!j.get("fps") || j.get("fps")->n != FPS) throw std::runtime_error("markers: fps must be 60");
    MarkerSet s;
    if (auto* src = j.get("source")) s.commit = src->str_or("commit");
    for (const auto& [key, c] : j.get("clips")->o) s.clips.push_back(clip_of(key, c));
    return s;
}

std::vector<std::string> check_markers(const MarkerClip& c, const Move& m) {
    std::vector<std::string> out;
    diff(out, c.key, "active", c.active, m.startup);
    diff(out, c.key, "recovery", c.recovery, m.startup + m.active);
    diff(out, c.key, "end", c.end, m.total);
    const Section* tell = m.find(Sec::Tell);
    diff(out, c.key, "tell", c.has_tell ? 1 : 0, tell ? 1 : 0);
    if (c.has_tell && tell) { diff(out, c.key, "tell.on", c.tell_on, tell->from); diff(out, c.key, "tell.off", c.tell_off, tell->to); }
    diff(out, c.key, "smear.from", c.smear_from, m.smear_from);
    diff(out, c.key, "smear.to", c.smear_to, m.smear_to);
    std::vector<const Section*> hits;
    for (const Section& s : m.sections) if (s.kind == Sec::Hit) hits.push_back(&s);
    diff(out, c.key, "hits", (int)c.hits.size(), (int)hits.size());
    for (size_t i = 0; i < c.hits.size() && i < hits.size(); ++i) {
        diff(out, c.key, "hit" + std::to_string(i) + ".on", c.hits[i].on, hits[i]->from);
        diff(out, c.key, "hit" + std::to_string(i) + ".off", c.hits[i].off, hits[i]->to);
    }
    return out;
}

}  // namespace raw::game::action
