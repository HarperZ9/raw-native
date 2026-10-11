// The clear coat's energy compensation: see src/renderer/pbr_internal.hpp (CoatComp) and
// evidence/m3-materials2-bounds.json, method note 1 of 2026-10-11.
#include "raw/renderer/pbr.hpp"
#include "pbr_internal.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr::detail {
namespace {
constexpr int J = Tables::kHats;
}  // namespace

CoatComp coatComp(const Material& m, const Tables& t, const Setup& s) {
    CoatComp cc;
    cc.c = std::clamp(m.clearcoat, 0.0, 1.0);
    if (cc.c <= 0.0 || m.specExact) return cc;
    cc.on = true;
    cc.rc = std::clamp(m.clearcoatRoughness, kMinRoughness, 1.0);
    cc.rq = s.r;
    for (int j = 0; j < J; ++j) cc.ec[j] = t.coef(j, cc.rc);   // E_c projected onto the hats
    const double mt = std::clamp(m.metallic, 0.0, 1.0), tr = std::clamp(m.transmission, 0.0, 1.0);
    cc.f0s = m.baseColor * mt + s.f0d * (1.0 - mt);
    cc.f90s = mt + s.f90 * (1.0 - mt);
    cc.kmsTot = s.kmsMetal * mt + s.kmsDiel * (1.0 - mt);
    cc.ebar = s.ebar;
    double sumG = 0.0, sumA = 0.0, sumB = 0.0;   // ec-weighted moments: 1, A and B
    for (int j = 0; j < J; ++j) {
        sumG += cc.ec[j] * t.G1(j);
        sumA += cc.ec[j] * (s.an ? t.GA4(j, s.r, s.k) : t.GA(j, cc.rq));
        sumB += cc.ec[j] * (s.an ? t.GB4(j, s.r, s.k) : t.GB(j, cc.rq));
    }
    cc.sMs = sumG - sumA - sumB;
    cc.dK = each([&](int k) { return ch(m.baseColor, k) * (1.0 - mt) * (1.0 - tr) / std::max(1e-12, 1.0 - ch(s.esAvg, k)); });
    cc.sD = each([&](int k) { return sumG - ch(s.f0d, k) * sumA - s.f90 * sumB - ch(s.kmsDiel, k) * cc.sMs; });
    cc.shade = m.sheenColor.max3();
    cc.sheenR = m.sheenRoughness;
    // Dbar = c (Qbar - c (E_c Q)bar), each part's means from the tables.
    double qa = 0.0, qb = 0.0, qqa = 0.0, qqb = 0.0;
    for (int j = 0; j < J; ++j) {
        qa += cc.ec[j] * (s.an ? t.HAbar4(j, s.r, s.k) : t.HAbar(j, cc.rq));
        qb += cc.ec[j] * (s.an ? t.HBbar4(j, s.r, s.k) : t.HBbar(j, cc.rq));
        for (int l = std::max(0, j - 1); l <= std::min(J - 1, j + 1); ++l) {   // hats overlap their neighbours only
            qqa += cc.ec[j] * cc.ec[l] * (s.an ? t.HAA4(j, l, s.r, s.k) : t.HAA(j, l, cc.rq));
            qqb += cc.ec[j] * cc.ec[l] * (s.an ? t.HBB4(j, l, s.r, s.k) : t.HBB(j, l, cc.rq));
        }
    }
    cc.dbar = each([&](int k) {
        const double f0 = ch(cc.f0s, k), km = ch(cc.kmsTot, k), dk = ch(cc.dK, k), sd = ch(cc.sD, k);
        const double qbar = f0 * qa + cc.f90s * qb + km * cc.sMs + dk * (1.0 - ch(s.esAvg, k)) * sd;
        const double eq = f0 * qqa + cc.f90s * qqb + km * cc.sMs * cc.sMs / std::max(1e-12, 1.0 - cc.ebar) + dk * sd * sd;
        return cc.c * (qbar - cc.c * eq);
    });
    return cc;
}

Rgb coatD(const CoatComp& cc, const Tables& t, const Setup& s, D3 w, double muCoat) {
    const double mu = std::fabs(w.z);
    double ha = 0.0, hb = 0.0;
    if (s.an) {
        const double ph = std::atan2(std::fabs(w.y), std::fabs(w.x));
        for (int j = 0; j < J; ++j) { ha += cc.ec[j] * t.HA4(j, mu, ph, s.r, s.k); hb += cc.ec[j] * t.HB4(j, mu, ph, s.r, s.k); }
    } else {
        for (int j = 0; j < J; ++j) { ha += cc.ec[j] * t.HA(j, mu, cc.rq); hb += cc.ec[j] * t.HB(j, mu, cc.rq); }
    }
    double a, b;
    split(s, t, w, a, b);
    const double msW = (1.0 - a - b) / std::max(1e-12, 1.0 - cc.ebar) * cc.sMs;
    const Rgb es = esAt(s, t, w);
    const double keep = cc.shade > 0.0 ? 1.0 - cc.shade * t.Sh(mu, cc.sheenR) : 1.0;   // sheen: a conservative share
    const double front = cc.c * (1.0 - cc.c * coatAlbedo(t, std::max(muCoat, 0.0), cc.rc)) * keep;
    return each([&](int k) {
        const double q = ch(cc.f0s, k) * ha + cc.f90s * hb + ch(cc.kmsTot, k) * msW + ch(cc.dK, k) * (1.0 - ch(es, k)) * ch(cc.sD, k);
        return front * std::max(q, 0.0);
    });
}

}  // namespace raw::pbr::detail
