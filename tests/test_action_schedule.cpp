// One schedule (docs/action-schedule.md, G6). Over the 40 contact traces (tests/data/action/contacts):
//   S1 every cue that belongs to a contact (contact, hitstop, shake, rumble, sound, effect, hitmarker) carries the
//      contact's sim tick, and every contact has its channels. Planted: a sound one tick late fails on every
//      contact; a hitmarker one tick late is caught.
//   S2 trail cues only on active frames, smear cues only on authored smear frames (in clip time). Planted: the
//      trail kept one frame into recovery, and a smear on the first recovery frame, are caught.
//   S3 contact hit-stop 0 light, 2 heavy, never over 4. Planted: heavy set to 5 is caught.
//   S7 each rumble cue equals the JS engine's rumble event for that blow (the recorded trace).
#include "check.hpp"
#include "raw/game/action/retime.hpp"
#include "raw/game/action/schedule.hpp"
#include <cmath>
#include <fstream>
#include <sstream>

using namespace raw::game::action;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/contacts/";

struct Tally { int trail_off = 0, smear_off = 0, trail = 0, smear = 0, hs_bad = 0, hs_over = 0, light = 0, heavy = 0, slip = 0; };

// S2 and S3 checked against the timeline as each tick is scheduled.
static void check_tick(const Schedule& s, size_t from, const TickView& v, Tally& t) {
    for (size_t i = from; i < s.cues.size(); i++) {
        const Cue& c = s.cues[i];
        if (c.ch == Channel::Trail || c.ch == Channel::Smear) {
            const Actor& F = v.tl->actors[(size_t)c.actor];
            const Move& m = v.tl->table().at(F.act.move);
            const int f = F.act.f;
            if (c.ch == Channel::Trail) { t.trail++; t.trail_off += !(f >= m.startup && f <= m.startup + m.active - 1); }
            else {
                t.smear++;
                const Section* tell = m.find(Sec::Tell);
                const double ct = tell && m.startup > tell->from ? clip_time(fit_telegraph(m.startup, tell->from), f) : f;
                t.smear_off += !(m.smear_to >= m.smear_from && std::floor(ct) >= m.smear_from && std::floor(ct) <= m.smear_to);
            }
        }
        if (c.ch == Channel::Hitstop) {
            const ContactRec& r = s.contacts[(size_t)c.contact];
            const int want = r.result == "evade" ? 0 : (r.result == "parry" || r.heavy) ? 2 : 0;
            t.hs_bad += (int)c.v[0] != want; t.hs_over += c.v[0] > 4;
            (r.result == "evade" ? t.slip : r.heavy ? t.heavy : t.light)++;
        }
    }
}

static Schedule run(const std::string& rec, const TraceTables& tables, const ScheduleOpts& so, Tally* t = nullptr, int heavy = -1) {
    Schedule s; ContactOpts co;
    if (heavy >= 0) co.tweak = [heavy](Config& c) { c.hitstop_heavy = heavy; };
    co.on_tick = [&](const TickView& v) { const size_t n = s.cues.size(); schedule_tick(s, v, so); if (t) check_tick(s, n, v, *t); };
    const TraceResult r = replay_contacts(rec, tables, co);
    if (!r.error.empty()) std::printf("replay: %s\n", r.error.c_str());
    return s;
}

int main() {
    const TraceTables tables = load_trace_tables(slurp(DIR + "tables.json"));
    const Json manifest = raw::story::parse_json(slurp(DIR + "manifest.json"));
    Tally t, late_trail, late_smear, plant5;
    long cues = 0, contacts = 0, bad = 0, sound_late = 0, mark_late = 0, rumble_js = 0, rumble_eq = 0;
    for (auto& [name, info] : manifest.o) {
        const std::string rec = slurp(DIR + name);
        const Schedule s = run(rec, tables, {}, &t);
        cues += (long)s.cues.size(); contacts += (long)s.contacts.size();
        for (auto& l : s.check_same_tick()) { if (bad < 5) std::printf("%s: %s\n", name.c_str(), l.c_str()); bad++; }
        ScheduleOpts a; a.sound_late = 1; sound_late += (long)run(rec, tables, a).check_same_tick().size();
        ScheduleOpts b; b.hitmarker_late = 1; mark_late += (long)run(rec, tables, b).check_same_tick().size();
        ScheduleOpts c; c.trail_extra = 1; run(rec, tables, c, &late_trail);
        ScheduleOpts d; d.smear_on_recovery = true; run(rec, tables, d, &late_smear);
        run(rec, tables, {}, &plant5, 5);
        // S7: the recorded JS rumble events against the rumble cues, in order
        std::vector<const Cue*> rc; for (auto& q : s.cues) if (q.ch == Channel::Rumble) rc.push_back(&q);
        std::istringstream in(rec); std::string line; size_t i = 0;
        while (std::getline(in, line)) {
            if (line.find("\"n\":\"rumble\"") == std::string::npos) continue;
            const Json r = raw::story::parse_json(line); const Json& d = *r.get("d");
            rumble_js++;
            if (i < rc.size() && rc[i]->tick == (int)r.get("tick")->n && rc[i]->v[0] == d.get("low")->n && rc[i]->v[1] == d.get("high")->n && rc[i]->v[2] == d.get("frames")->n) rumble_eq++;
            i++;
        }
    }
    std::printf("S1: %ld cues, %ld contacts over %zu traces, %ld same-tick exceptions\n", cues, contacts, manifest.o.size(), bad);
    std::printf("S1 planted: sound one tick late gives %ld exceptions (contacts %ld); hitmarker one tick late gives %ld\n", sound_late, contacts, mark_late);
    std::printf("S2: %d trail cues, %d off an active frame; %d smear cues, %d off an authored frame\n", t.trail, t.trail_off, t.smear, t.smear_off);
    std::printf("S2 planted: trail into recovery %d off; smear on recovery %d off\n", late_trail.trail_off, late_smear.smear_off);
    std::printf("S3: hit-stop over %d light, %d heavy, %d slipped contacts: %d wrong, %d over 4; planted heavy 5: %d over 4\n", t.light, t.heavy, t.slip, t.hs_bad, t.hs_over, plant5.hs_over);
    std::printf("S7: %ld of %ld JS rumble events equal their cue\n", rumble_eq, rumble_js);
    CHECK(contacts > 0 && bad == 0);
    CHECK(sound_late == contacts && mark_late == contacts);
    CHECK(t.trail > 0 && t.trail_off == 0 && t.smear > 0 && t.smear_off == 0);
    CHECK(late_trail.trail_off > 0 && late_smear.smear_off > 0);
    CHECK(t.heavy > 0 && t.light > 0 && t.hs_bad == 0 && t.hs_over == 0 && plant5.hs_over > 0);
    CHECK(rumble_js > 0 && rumble_eq == rumble_js);
    return raw_test_summary();
}
