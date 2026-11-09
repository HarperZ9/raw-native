// The swing decides the hit (docs/action-schedule.md). P2: the 40 contact traces recorded from Low Water's JS
// engine (tests/data/action/contacts, tools/action/record_contacts.mjs), where W.step found every contact with
// capsules, replay byte-identical in C++, the C++ finding every contact with raw/game/action/contact.hpp.
// Planted: ring contact in place of capsules, a 0.06 m capsule in place of 0.05 m, and a flipped byte must each
// break at least one trace. P3: the shared-math kernel against G3's capsule (hitvol.hpp, a posed skeleton on
// standard math) over a grid of targets: they agree except within rounding of the contact boundary.
#include "check.hpp"
#include "raw/core/sha256.hpp"
#include "raw/game/action/contact_trace.hpp"
#include "raw/game/action/detmath.hpp"
#include "raw/game/action/hitvol.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace raw::game::action;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const std::string DATA = std::string(RAW_SOURCE_DIR) + "/tests/data/action/";

static void parity() {
    const std::string dir = DATA + "contacts/";
    const TraceTables tables = load_trace_tables(slurp(dir + "tables.json"));
    const Json manifest = raw::story::parse_json(slurp(dir + "manifest.json"));
    int same = 0, hashed = 0, ring = 0, wide = 0, contacts = 0;
    std::string first;
    for (auto& [name, info] : manifest.o) {
        const std::string rec = slurp(dir + name);
        hashed += raw::sha256Hex(rec) == info.str_or("sha256");
        const TraceResult r = replay_contacts(rec, tables);
        if (!r.error.empty()) std::printf("%s: %s\n", name.c_str(), r.error.c_str());
        const bool ok = r.error.empty() && r.trace == rec;
        if (!ok) std::printf("%s differs at line %zu\n", name.c_str(), first_line_difference(r.trace, rec));
        same += ok;
        ContactOpts rings; rings.rings = true;
        ring += replay_contacts(rec, tables, rings).trace != rec;
        ContactOpts w; w.rc = 0.06;
        wide += replay_contacts(rec, tables, w).trace != rec;
        if (auto* c = info.get("contacts")) contacts += (int)c->n;
        if (first.empty()) first = name;
    }
    std::printf("contact traces: %d of %zu byte-identical, %d of %zu match the manifest hash, %d contacts found by capsules\n",
                same, manifest.o.size(), hashed, manifest.o.size(), contacts);
    std::printf("planted: ring contact breaks %d of %zu, a 0.06 m capsule breaks %d of %zu\n", ring, manifest.o.size(), wide, manifest.o.size());
    CHECK(manifest.o.size() == 40 && same == 40 && hashed == 40 && contacts > 0);
    CHECK(ring > 0);
    CHECK(wide > 0);
    std::string rec = slurp(dir + first), flipped = rec;
    const size_t at = flipped.find("\"t\":\"ast\"", flipped.size() / 2);
    flipped[flipped.find(',', at + 20) - 1] ^= 1;
    CHECK(first_line_difference(replay_contacts(flipped, tables).trace, flipped) > 0);
}

// P3: every ring of the marker clips (the moves.json expansion), targets on a grid of distance and bearing, every
// live tick. A disagreement counts as rounding when the kernel's nearest sample sits within 1e-9 m of the boundary.
static void against_g3() {
    const MarkerSet ms = load_markers(slurp(DATA + "clip-markers.json"));
    long total = 0, agree = 0, rounding = 0, other = 0;
    const double R = 0.35, H = 1.8;
    for (const MarkerClip& c : ms.clips) {
        if (c.key.rfind("inner/", 0) == 0) continue;
        for (const HitMarker& hm : c.hits) {
            Ring h; h.on = hm.on; h.off = hm.off;
            for (int j = 0; j < 2; j++) { h.arc[j] = hm.arc[j]; h.r[j] = hm.r[j]; h.y[j] = hm.y[j]; }
            const HitVolume v = volume_from_ring(hm);
            for (double d = 0; d <= hm.r[1] + R + 1e-9; d += 0.1)
                for (int deg = -180; deg < 180; deg += 6)
                    for (int f = hm.on; f <= hm.off; f++) {
                        const double x = d * std::cos(deg * DPI / 180), z = d * std::sin(deg * DPI / 180);
                        const Body A{0, 0, 0, R, H}, T{x, z, 0, R, H};
                        const bool k = capsule_contact(A, h, T, f), g = capsule_hits(v, f, x, z, R, H);
                        total++;
                        if (k == g) { agree++; continue; }
                        const int n = ring_substeps(h);
                        double best = 1e9;
                        for (int s = 0; s <= n; s++) best = std::min(best, std::fabs(std::sqrt(blade_dist2(h, ring_yaw(h, f, s, n), x, z)) - (R + CONTACT_RC)));
                        (best <= 1e-9 ? rounding : other)++;
                    }
        }
    }
    std::printf("kernel against G3 capsules: %ld targets, %ld agree (%.5f%%), %ld differ within 1e-9 m of the boundary, %ld otherwise\n",
                total, agree, 100.0 * agree / std::max(1L, total), rounding, other);
    CHECK(total > 0 && (double)agree / total >= 0.9999 && other == 0);
}

int main() {
    parity();
    against_g3();
    return raw_test_summary();
}
