// raw-native GPU material model: the float32 form of raw/renderer/pbr.hpp.
// The same model term for term; the CPU reference is float64, and the parity check
// (raw/renderer/pbr_parity.hpp) compares the two within evidence/m3-materials-bounds.json.
// Shared code sits above the first pass marker; each pass follows it.
// Translator subset (scripts/wgsl_to_hlsl.py): every let and var typed, one statement per
// line, no struct values (materials are read from the case buffer by index).

struct PbrParams {
    count: u32, flags: u32, nlights: u32, env_size: u32,
    env_levels: u32, sh_off: u32, pad0: u32, pad1: u32,
    grid: vec4f,
    misc: vec4f,
}

const PI: f32 = 3.14159265358979;
const MIN_ROUGH: f32 = 0.045;
const MIN_SHEEN: f32 = 0.07;
const THETA_PAD: f32 = 0.05;
const ROUGH_MAX: f32 = 1.05;
const N_THETA: u32 = 128u;
const N_ROUGH: u32 = 128u;
const N_SH: u32 = 64u;
const AN_MU: u32 = 32u;
const AN_PHI: u32 = 8u;
const AN_ROUGH: u32 = 24u;
const AN_K: u32 = 12u;
const HARMONICS: u32 = 8u;
const HARMONICS_SPEC: u32 = 2u;
// Offsets of each table in the one table buffer (floats); raw/renderer/pbr_parity.hpp packs them.
const OFF_A: u32 = 0u;
const OFF_B: u32 = 16384u;
const OFF_AAVG: u32 = 32768u;
const OFF_BAVG: u32 = 32896u;
const OFF_SH: u32 = 33024u;
const OFF_A4: u32 = 37120u;
const OFF_B4: u32 = 110848u;
const OFF_AAVG2: u32 = 184576u;
const OFF_BAVG2: u32 = 184864u;
const OFF_DOM: u32 = 185152u;
// The iridescence sensitivity table after the dominant-direction grid: 4,001 entries of 6 floats.
const OFF_SENS: u32 = 189248u;
const SENS_N: u32 = 4001u;
const SENS_STEP: f32 = 10.0;
const N_DOM: u32 = 64u;
// A case: 44 floats. Material fields in pbr.hpp order, then wo and wi; the anisotropy
// rotation arrives as its cosine (27) and sine (35), computed on the host.
const CASE: u32 = 44u;
const FLAG_NO_MS: u32 = 1u;

fn pdot(a: vec3f, b: vec3f) -> f32 { return a.x * b.x + a.y * b.y + a.z * b.z; }
fn pnorm(a: vec3f) -> vec3f { let l: f32 = sqrt(pdot(a, a)); return a / l; }
fn pmax3(c: vec3f) -> f32 { return max(c.x, max(c.y, c.z)); }

fn alpha_of(r: f32) -> f32 { let c: f32 = clamp(r, MIN_ROUGH, 1.0); return c * c; }
fn ggx_d(h: vec3f, ax: f32, ay: f32) -> f32 {
    if (h.z <= 0.0) { return 0.0; }
    let u: f32 = h.x / ax;
    let v: f32 = h.y / ay;
    let s: f32 = u * u + v * v + h.z * h.z;
    return 1.0 / (PI * ax * ay * s * s);
}
fn smith_v(o: vec3f, i: vec3f, ax: f32, ay: f32) -> f32 {
    let qo: f32 = sqrt(ax * ax * o.x * o.x + ay * ay * o.y * o.y + o.z * o.z);
    let qi: f32 = sqrt(ax * ax * i.x * i.x + ay * ay * i.y * i.y + i.z * i.z);
    return 0.5 / (abs(i.z) * qo + abs(o.z) * qi);
}
fn schlick(f0: f32, f90: f32, c: f32) -> f32 {
    let m: f32 = 1.0 - clamp(c, 0.0, 1.0);
    let m5: f32 = m * m * m * m * m;
    return f0 + (f90 - f0) * m5;
}
fn charlie_d(h: vec3f, rs: f32) -> f32 {
    let r: f32 = clamp(rs, MIN_SHEEN, 1.0);
    let inv: f32 = 1.0 / (r * r);
    let s2: f32 = h.x * h.x + h.y * h.y;
    return (2.0 + inv) * pow(s2, 0.5 * inv) / (2.0 * PI);
}
fn sheen_l(x: f32, a: f32) -> f32 {
    let t: f32 = (1.0 - a) * (1.0 - a);
    let ca: f32 = 21.5473 + (25.3245 - 21.5473) * t;
    let cb: f32 = 3.82987 + (3.32435 - 3.82987) * t;
    let cc: f32 = 0.19823 + (0.16801 - 0.19823) * t;
    let cd: f32 = -1.97760 + (-1.27393 + 1.97760) * t;
    let ce: f32 = -4.32054 + (-4.85967 + 4.32054) * t;
    return ca / (1.0 + cb * pow(x, cc)) + cd * x + ce;
}
fn sheen_lambda(c: f32, a: f32) -> f32 {
    if (abs(c) < 0.5) { return exp(sheen_l(c, a)); }
    return exp(2.0 * sheen_l(0.5, a) - sheen_l(1.0 - c, a));
}
fn charlie_v(mo: f32, mi: f32, rs: f32) -> f32 {
    let r: f32 = clamp(rs, MIN_SHEEN, 1.0);
    let a: f32 = r * r;
    return 1.0 / ((1.0 + sheen_lambda(mo, a) + sheen_lambda(mi, a)) * (4.0 * mo * mi));
}
fn walter(win: vec3f, eta_in: f32, wout: vec3f, eta_out: f32, ax: f32, ay: f32, f0: f32) -> f32 {
    if (win.z * wout.z >= 0.0) { return 0.0; }
    var h: vec3f = -(eta_in * win + eta_out * wout);
    let l: f32 = sqrt(pdot(h, h));
    if (l <= 0.0) { return 0.0; }
    h = h / l;
    if (h.z < 0.0) { h = -h; }
    let ih: f32 = pdot(win, h);
    let oh: f32 = pdot(wout, h);
    if (ih * win.z <= 0.0 || oh * wout.z <= 0.0) { return 0.0; }
    let clow: f32 = select(abs(oh), abs(ih), eta_in <= eta_out);
    let fr: f32 = schlick(f0, 1.0, clow);
    let den: f32 = eta_in * ih + eta_out * oh;
    let g2: f32 = 4.0 * abs(win.z) * abs(wout.z) * smith_v(win, wout, ax, ay);
    return abs(ih) * abs(oh) / (abs(win.z) * abs(wout.z)) * eta_out * eta_out * (1.0 - fr) * g2 * ggx_d(h, ax, ay) / (den * den);
}

// Tables: bilinear on (theta, roughness) rows, as raw::pbr::Tables reads them.
fn locate(x: f32, n: u32, i0: ptr<function, u32>, f: ptr<function, f32>) {
    let c: f32 = clamp(x, 0.0, f32(n) - 1.0);
    *i0 = min(u32(c), n - 2u);
    *f = c - f32(*i0);
}
fn theta_x(mu: f32, n: u32) -> f32 {
    let th: f32 = acos(clamp(mu, 0.0, 1.0));
    return (th + THETA_PAD) / (PI / 2.0 + THETA_PAD) * f32(n - 1u);
}
fn read2(off: u32, nt: u32, nr: u32, mu: f32, r: f32) -> f32 {
    var x0: u32 = 0u; var fx: f32 = 0.0; var y0: u32 = 0u; var fy: f32 = 0.0;
    locate(theta_x(mu, nt), nt, &x0, &fx);
    locate(r / ROUGH_MAX * f32(nr - 1u), nr, &y0, &fy);
    let a: u32 = off + y0 * nt + x0;
    return (T[a] * (1.0 - fx) + T[a + 1u] * fx) * (1.0 - fy) + (T[a + nt] * (1.0 - fx) + T[a + nt + 1u] * fx) * fy;
}
fn read1(off: u32, n: u32, r: f32) -> f32 {
    var y0: u32 = 0u; var f: f32 = 0.0;
    locate(r / ROUGH_MAX * f32(n - 1u), n, &y0, &f);
    return T[off + y0] * (1.0 - f) + T[off + y0 + 1u] * f;
}
fn read4(off: u32, mu: f32, phi_in: f32, r: f32, k: f32) -> f32 {
    var ph: f32 = abs(phi_in);
    ph = ph - floor(ph / PI) * PI;
    if (ph > 0.5 * PI) { ph = PI - ph; }
    var i0: u32 = 0u; var f0: f32 = 0.0; var i1: u32 = 0u; var f1: f32 = 0.0;
    var i2: u32 = 0u; var f2: f32 = 0.0; var i3: u32 = 0u; var f3: f32 = 0.0;
    locate(theta_x(mu, AN_MU), AN_MU, &i0, &f0);
    locate(ph / (0.5 * PI) * f32(AN_PHI - 1u), AN_PHI, &i1, &f1);
    locate(r / ROUGH_MAX * f32(AN_ROUGH - 1u), AN_ROUGH, &i2, &f2);
    locate(k * f32(AN_K - 1u), AN_K, &i3, &f3);
    var s: f32 = 0.0;
    for (var c: u32 = 0u; c < 16u; c++) {
        let b0: u32 = c & 1u; let b1: u32 = (c >> 1u) & 1u; let b2: u32 = (c >> 2u) & 1u; let b3: u32 = (c >> 3u) & 1u;
        let w: f32 = select(1.0 - f0, f0, b0 == 1u) * select(1.0 - f1, f1, b1 == 1u) * select(1.0 - f2, f2, b2 == 1u) * select(1.0 - f3, f3, b3 == 1u);
        s = s + w * T[off + (((i3 + b3) * AN_ROUGH + i2 + b2) * AN_PHI + i1 + b1) * AN_MU + i0 + b0];
    }
    return s;
}
fn read2rk(off: u32, r: f32, k: f32) -> f32 {
    var ir: u32 = 0u; var fr: f32 = 0.0; var ik: u32 = 0u; var fk: f32 = 0.0;
    locate(r / ROUGH_MAX * f32(AN_ROUGH - 1u), AN_ROUGH, &ir, &fr);
    locate(k * f32(AN_K - 1u), AN_K, &ik, &fk);
    let a: u32 = off + ik * AN_ROUGH + ir;
    return (T[a] * (1.0 - fr) + T[a + 1u] * fr) * (1.0 - fk) + (T[a + AN_ROUGH] * (1.0 - fr) + T[a + AN_ROUGH + 1u] * fr) * fk;
}
// The sheen table's view axis is sqrt(mu) (raw::pbr::Tables::Sh).
fn read_sh(mu: f32, r: f32) -> f32 {
    var x0: u32 = 0u; var fx: f32 = 0.0; var y0: u32 = 0u; var fy: f32 = 0.0;
    locate(sqrt(clamp(mu, 0.0, 1.0)) * f32(N_SH - 1u), N_SH, &x0, &fx);
    locate(r / ROUGH_MAX * f32(N_SH - 1u), N_SH, &y0, &fy);
    let a: u32 = OFF_SH + y0 * N_SH + x0;
    return (T[a] * (1.0 - fx) + T[a + 1u] * fx) * (1.0 - fy) + (T[a + N_SH] * (1.0 - fx) + T[a + N_SH + 1u] * fx) * fy;
}
fn tab_e(mu: f32, r: f32) -> f32 { return read2(OFF_A, N_THETA, N_ROUGH, mu, r) + read2(OFF_B, N_THETA, N_ROUGH, mu, r); }
fn tab_eavg(r: f32) -> f32 { return read1(OFF_AAVG, N_ROUGH, r) + read1(OFF_BAVG, N_ROUGH, r); }

// Iridescence (Belcour and Barla 2017), eight harmonics as raw::pbr::iridescentFresnel.
fn xyz_to_709(x: f32, y: f32, z: f32) -> vec3f {
    return vec3f(3.2404542 * x - 1.5371385 * y - 0.4985314 * z, -0.9692660 * x + 1.8760108 * y + 0.0415560 * z, 0.0556434 * x - 0.2040259 * y + 1.0572252 * z);
}
fn sensitivity(opd: f32, shift: vec3f) -> vec3f {
    let ph: f32 = 2.0 * PI * opd * 1.0e-9;
    let val: vec3f = vec3f(5.4856e-13, 4.4201e-13, 5.2481e-13);
    let pos: vec3f = vec3f(1.6810e+06, 1.7953e+06, 2.2084e+06);
    let vr: vec3f = vec3f(4.3278e+09, 9.3046e+09, 6.6121e+09);
    var xyz: vec3f = val * sqrt(2.0 * PI * vr) * cos(pos * ph + shift) * exp(-ph * ph * vr);
    xyz.x = xyz.x + 9.7470e-14 * sqrt(2.0 * PI * 4.5282e+09) * cos(2.2399e+06 * ph + shift.x) * exp(-4.5282e+09 * ph * ph);
    xyz = xyz / 1.0685e-7;
    return xyz_to_709(xyz.x, xyz.y, xyz.z);
}
fn ior_to_f0(t: f32, i: f32) -> f32 { let q: f32 = (t - i) / (t + i); return q * q; }
// Slot 20 of a material holds the flags: 1 volume, 2 the extension texts' forms.
fn is_volume(b: u32) -> bool { return (u32(C[b + 20u]) & 1u) != 0u; }
fn spec_exact(b: u32) -> bool { return (u32(C[b + 20u]) & 2u) != 0u; }
fn harm(b: u32) -> u32 { return select(HARMONICS, HARMONICS_SPEC, spec_exact(b)); }
// The sensitivity tabulated from the references' colour matching fit (raw::pbr, kSensMax and
// kSensStep): per entry the real parts of three channels, then the imaginary parts.
fn sens_tab(opd: f32, shift: vec3f) -> vec3f {
    let u: f32 = clamp(opd / SENS_STEP, 0.0, f32(SENS_N - 1u));
    let j: u32 = min(u32(u), SENS_N - 2u);
    let f: f32 = u - f32(j);
    let a: u32 = OFF_SENS + j * 6u;
    let re: vec3f = vec3f(T[a], T[a + 1u], T[a + 2u]) * (1.0 - f) + vec3f(T[a + 6u], T[a + 7u], T[a + 8u]) * f;
    let im: vec3f = vec3f(T[a + 3u], T[a + 4u], T[a + 5u]) * (1.0 - f) + vec3f(T[a + 9u], T[a + 10u], T[a + 11u]) * f;
    return re * cos(shift) - im * sin(shift);
}
// The Belcour-Barla series for one set of interface terms.
fn irid_series(r12: f32, t121: f32, phi21: f32, r23: vec3f, phi23: vec3f, opd: f32, harmonics: u32, tabulated: bool) -> vec3f {
    let phi: vec3f = vec3f(phi21) + phi23;
    let r123sq: vec3f = clamp(r12 * r23, vec3f(1e-5), vec3f(0.9999));
    let r123: vec3f = sqrt(r123sq);
    let rs: vec3f = t121 * t121 * r23 / (vec3f(1.0) - r123sq);
    var acc: vec3f = vec3f(r12) + rs;
    var cm: vec3f = rs - vec3f(t121);
    for (var m: u32 = 1u; m <= harmonics; m++) {
        cm = cm * r123;
        let sm: vec3f = select(sensitivity(f32(m) * opd, f32(m) * phi), sens_tab(f32(m) * opd, f32(m) * phi), tabulated);
        acc = acc + cm * 2.0 * sm;
    }
    return acc;
}
// One polarization at the base: R23 and its phase per channel, exact (complex past total
// internal reflection), as raw::pbr's polarized path.
// Returns (R23, phase).
fn r23_pol(n2: f32, cos2: f32, n3: f32, p: bool) -> vec2f {
    let sin2sq: f32 = 1.0 - cos2 * cos2;
    let c3sq: f32 = 1.0 - (n2 / n3) * (n2 / n3) * sin2sq;
    if (c3sq >= 0.0) {
        let c3: f32 = sqrt(c3sq);
        let r: f32 = select((n2 * cos2 - n3 * c3) / (n2 * cos2 + n3 * c3), (n3 * cos2 - n2 * c3) / (n3 * cos2 + n2 * c3), p);
        return vec2f(r * r, select(PI, 0.0, r >= 0.0));
    }
    let k: f32 = sqrt(-c3sq);
    return vec2f(1.0, select(-2.0 * atan2(n3 * k, n2 * cos2), -2.0 * atan2(n2 * k, n3 * cos2), p));
}
// Thin-film Fresnel (raw::pbr::iridescentFresnel): the polarized path with the tabulated
// sensitivity by default; the extension text's Schlick interfaces and Gaussian fit when the
// material asks for the specification's forms or the base is a conductor (F0 above 0.25).
fn irid_f(film: f32, d: f32, f0: vec3f, cos1: f32, b: u32) -> vec3f {
    let s2: f32 = (1.0 / film) * (1.0 / film) * (1.0 - cos1 * cos1);
    let c2sq: f32 = 1.0 - s2;
    if (c2sq < 0.0) { return vec3f(1.0); }
    let cos2: f32 = sqrt(c2sq);
    let sf: vec3f = sqrt(f0 + vec3f(0.0001));
    let base: vec3f = (vec3f(1.0) + sf) / (vec3f(1.0) - sf);
    let opd: f32 = 2.0 * film * d * cos2;
    let harmonics: u32 = harm(b);
    if (spec_exact(b) || pmax3(f0) > 0.25) {
        let r12: f32 = schlick(ior_to_f0(film, 1.0), 1.0, cos1);
        let phi21: f32 = PI - select(0.0, PI, film < 1.0);
        let r23: vec3f = vec3f(schlick(ior_to_f0(base.x, film), 1.0, cos2), schlick(ior_to_f0(base.y, film), 1.0, cos2), schlick(ior_to_f0(base.z, film), 1.0, cos2));
        let phi23: vec3f = vec3f(select(0.0, PI, base.x < film), select(0.0, PI, base.y < film), select(0.0, PI, base.z < film));
        return max(irid_series(r12, 1.0 - r12, phi21, r23, phi23, opd, harmonics, false), vec3f(0.0));
    }
    var acc: vec3f = vec3f(0.0);
    for (var pol: u32 = 0u; pol < 2u; pol++) {
        let p: bool = pol == 1u;
        let r: f32 = select((cos1 - film * cos2) / (cos1 + film * cos2), (film * cos1 - cos2) / (film * cos1 + cos2), p);
        let r12: f32 = r * r;
        let phi21: f32 = select(PI, 0.0, -r >= 0.0);
        let qx: vec2f = r23_pol(film, cos2, base.x, p);
        let qy: vec2f = r23_pol(film, cos2, base.y, p);
        let qz: vec2f = r23_pol(film, cos2, base.z, p);
        acc = acc + 0.5 * irid_series(r12, 1.0 - r12, phi21, vec3f(qx.x, qy.x, qz.x), vec3f(qx.y, qy.y, qz.y), opd, harmonics, true);
    }
    return max(acc, vec3f(0.0));
}
// Material evaluation from a case or sample buffer C (every pass of this module binds the
// uniform P as 0, the material buffer C as 1 and the tables T as 2).
// Slot 20 holds the flags: 1 volume, 2 the extension texts' forms (raw::pbr::Material::specExact).
fn cv3(b: u32, k: u32) -> vec3f { return vec3f(C[b + k], C[b + k + 1u], C[b + k + 2u]); }
fn kms(favg: f32, ebar: f32) -> f32 { return favg * favg * ebar / (1.0 - favg * (1.0 - ebar)); }
fn kms3(f0: vec3f, f90: f32, ebar: f32) -> vec3f {
    let fa: vec3f = f0 + (vec3f(f90) - f0) / 21.0;
    return vec3f(kms(fa.x, ebar), kms(fa.y, ebar), kms(fa.z, ebar));
}
// Split albedo (A, B) for a direction in the anisotropy frame.
fn split_ab(w: vec3f, r: f32, k: f32) -> vec2f {
    let mu: f32 = abs(w.z);
    if (k > 0.0) {
        let ph: f32 = atan2(abs(w.y), abs(w.x));
        return vec2f(read4(OFF_A4, mu, ph, r, k), read4(OFF_B4, mu, ph, r, k));
    }
    return vec2f(read2(OFF_A, N_THETA, N_ROUGH, mu, r), read2(OFF_B, N_THETA, N_ROUGH, mu, r));
}
fn es_at(ab: vec2f, f0d: vec3f, f90: f32, kd: vec3f) -> vec3f {
    return f0d * ab.x + vec3f(f90 * ab.y) + (1.0 - ab.x - ab.y) * kd;
}
fn fresnel3(f0: vec3f, f90: f32, voh: f32, b: u32) -> vec3f {
    let f: vec3f = vec3f(schlick(f0.x, f90, voh), schlick(f0.y, f90, voh), schlick(f0.z, f90, voh));
    let iri: f32 = C[b + 28u];
    if (iri <= 0.0) { return f; }
    return f * (1.0 - iri) + irid_f(C[b + 29u], C[b + 30u], f0, voh, b) * iri;
}
// Sheen and clearcoat over the base result fb (raw::pbr::evalTerms).
fn layers(b: u32, wo: vec3f, wi: vec3f, fb: vec3f, ms_on: f32) -> vec3f {
    var f: vec3f = fb;
    let shc: vec3f = cv3(b, 15u);
    let sh: f32 = pmax3(shc);
    let rs: f32 = C[b + 18u];
    if (sh > 0.0) {
        let a_sheen: f32 = min(1.0 - sh * read_sh(wo.z, rs), 1.0 - sh * read_sh(abs(wi.z), rs));
        f = f * a_sheen;
        if (wi.z > 0.0) { f = f + shc * (charlie_d(pnorm(wo + wi), rs) * charlie_v(wo.z, wi.z, rs)); }
    }
    let c: f32 = clamp(C[b + 10u], 0.0, 1.0);
    if (c <= 0.0) { return f; }
    let nc: vec3f = pnorm(cv3(b, 12u));
    let mo: f32 = pdot(nc, wo);
    let mi: f32 = pdot(nc, wi);
    let rc: f32 = clamp(C[b + 11u], MIN_ROUGH, 1.0);
    let a: f32 = rc * rc;
    let co: f32 = max(mo, 0.0);
    let ci: f32 = select(abs(mi), max(mi, 0.0), wi.z > 0.0);
    let kc: f32 = kms(0.04 + 0.96 / 21.0, tab_eavg(rc));
    let eco: f32 = 0.04 * read2(OFF_A, N_THETA, N_ROUGH, co, rc) + read2(OFF_B, N_THETA, N_ROUGH, co, rc) + (1.0 - tab_e(co, rc)) * kc;
    let eci: f32 = 0.04 * read2(OFF_A, N_THETA, N_ROUGH, ci, rc) + read2(OFF_B, N_THETA, N_ROUGH, ci, rc) + (1.0 - tab_e(ci, rc)) * kc;
    let fr_v: f32 = schlick(0.04, 1.0, co);
    f = f * select((1.0 - c * eco) * (1.0 - c * eci), 1.0 - c * fr_v, spec_exact(b));
    if (mo > 0.0 && mi > 0.0 && wi.z > 0.0) {
        let h: vec3f = pnorm(wo + wi);
        let hn: f32 = pdot(h, nc);
        let cx: f32 = h.y * nc.z - h.z * nc.y;
        let cy: f32 = h.z * nc.x - h.x * nc.z;
        let cz: f32 = h.x * nc.y - h.y * nc.x;
        let s2: f32 = cx * cx + cy * cy + cz * cz;
        let den: f32 = s2 + a * a * hn * hn;
        let d: f32 = select(0.0, a * a / (PI * den * den), hn > 0.0);
        let v: f32 = 0.5 / (mi * sqrt(mo * mo * (1.0 - a * a) + a * a) + mo * sqrt(mi * mi * (1.0 - a * a) + a * a));
        let ms: f32 = (1.0 - tab_e(mo, rc)) * (1.0 - tab_e(mi, rc)) / (PI * (1.0 - tab_eavg(rc)));
        if (spec_exact(b)) { return f + vec3f(c * fr_v * d * v); }
        f = f + vec3f(c * d * v * schlick(0.04, 1.0, pdot(wo, h)) + c * kc * ms * ms_on);
    }
    return f;
}
// Coat over sheen over base, as raw::pbr::evalTerms; returns the total f.
fn eval_case(b: u32, wo: vec3f, wi: vec3f) -> vec3f {
    if (wo.z <= 0.0 || wi.z == 0.0) { return vec3f(0.0); }
    let ms_on: f32 = select(1.0, 0.0, (P.flags & FLAG_NO_MS) != 0u);
    let base: vec3f = cv3(b, 0u);
    let mt: f32 = clamp(C[b + 3u], 0.0, 1.0);
    let rr: f32 = clamp(C[b + 4u], MIN_ROUGH, 1.0);
    let ak: f32 = clamp(C[b + 26u], 0.0, 1.0);
    let a: f32 = alpha_of(C[b + 4u]);
    let ax: f32 = a + (1.0 - a) * ak * ak;
    let ay: f32 = a;
    let abar: f32 = select(read1(OFF_AAVG, N_ROUGH, rr), read2rk(OFF_AAVG2, rr, ak), ak > 0.0);
    let bbar: f32 = select(read1(OFF_BAVG, N_ROUGH, rr), read2rk(OFF_BAVG2, rr, ak), ak > 0.0);
    let ebar: f32 = abar + bbar;
    let ior: f32 = C[b + 5u];
    let q: f32 = (ior - 1.0) / (ior + 1.0);
    let f90: f32 = C[b + 6u];
    let f0d: vec3f = min(q * q * cv3(b, 7u), vec3f(1.0)) * f90;
    let km: vec3f = kms3(base, 1.0, ebar);
    let kd: vec3f = kms3(f0d, f90, ebar);
    let es_avg: vec3f = f0d * abar + vec3f(f90 * bbar) + (1.0 - ebar) * kd;
    let cr: f32 = C[b + 27u];
    let sr: f32 = C[b + 35u];
    let o: vec3f = vec3f(cr * wo.x + sr * wo.y, -sr * wo.x + cr * wo.y, wo.z);
    let i: vec3f = vec3f(cr * wi.x + sr * wi.y, -sr * wi.x + cr * wi.y, wi.z);
    let abo: vec2f = split_ab(o, rr, ak);
    let abi: vec2f = split_ab(i, rr, ak);
    let es_o: vec3f = es_at(abo, f0d, f90, kd);
    let es_i: vec3f = es_at(abi, f0d, f90, kd);
    let tr: f32 = clamp(C[b + 19u], 0.0, 1.0);
    var f: vec3f = vec3f(0.0);
    if (i.z > 0.0) {
        let h: vec3f = pnorm(o + i);
        let dv: f32 = ggx_d(h, ax, ay) * smith_v(o, i, ax, ay);
        let voh: f32 = pdot(o, h);
        let ms: f32 = (1.0 - abo.x - abo.y) * (1.0 - abi.x - abi.y) / (PI * (1.0 - ebar)) * ms_on;
        f = (fresnel3(base, 1.0, voh, b) * mt + fresnel3(f0d, f90, voh, b) * (1.0 - mt)) * dv;
        f = f + (km * mt + kd * (1.0 - mt)) * ms;
        var w: vec3f = (vec3f(1.0) - es_o) * (vec3f(1.0) - es_i) / (vec3f(1.0) - es_avg);
        let iri: f32 = C[b + 28u];
        if (iri > 0.0) {
            let mo: f32 = pmax3(irid_f(C[b + 29u], C[b + 30u], f0d, o.z, b));
            let mi: f32 = pmax3(irid_f(C[b + 29u], C[b + 30u], f0d, i.z, b));
            w = w * (1.0 - iri) + vec3f((1.0 - mo) * (1.0 - mi) * iri);
        }
        f = f + base * w * ((1.0 - mt) * (1.0 - tr) / PI);
    } else if (tr > 0.0 && mt < 1.0) {
        if (!is_volume(b)) {
            let im: vec3f = vec3f(i.x, i.y, -i.z);
            let h: vec3f = pnorm(o + im);
            let abm: vec2f = split_ab(im, rr, ak);
            let g: f32 = ggx_d(h, ax, ay) * smith_v(o, im, ax, ay) + (1.0 - abo.x - abo.y) * (1.0 - abm.x - abm.y) / (PI * (1.0 - ebar)) * ms_on;
            f = base * min(vec3f(1.0) - es_o, vec3f(1.0) - es_i) * (g * tr * (1.0 - mt));
        } else {
            var att: vec3f = vec3f(1.0);
            if (C[b + 22u] > 0.0 && C[b + 21u] > 0.0) {
                let path: f32 = select(C[b + 21u] / max(abs(i.z), 1e-6), C[b + 21u], spec_exact(b));
                att = exp(log(max(cv3(b, 23u), vec3f(1e-30))) / C[b + 22u] * path);
            }
            let wt: vec3f = vec3f(walter(i, ior, o, 1.0, ax, ay, f0d.x), walter(i, ior, o, 1.0, ax, ay, f0d.y), walter(i, ior, o, 1.0, ax, ay, f0d.z));
            f = base * att * wt * (tr * (1.0 - mt));
        }
    }
    return layers(b, wo, wi, f, ms_on);
}

//@pass pbr_eval
// Evaluates f * |cos theta_i| and the emission for each case (raw::pbr::eval and emission).
// One invocation per case.
@group(0) @binding(0) var<uniform> P: PbrParams;
@group(0) @binding(1) var<storage, read> C: array<f32>;
@group(0) @binding(2) var<storage, read> T: array<f32>;
@group(0) @binding(3) var<storage, read_write> R: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let k: u32 = gid.x;
    if (k >= P.count) { return; }
    let b: u32 = k * CASE;
    let wo: vec3f = cv3(b, 36u);
    let wi: vec3f = cv3(b, 39u);
    let f: vec3f = eval_case(b, wo, wi) * abs(wi.z);
    let e: vec3f = cv3(b, 31u) * C[b + 34u];
    let o: u32 = k * 6u;
    R[o] = f.x; R[o + 1u] = f.y; R[o + 2u] = f.z;
    R[o + 3u] = e.x; R[o + 4u] = e.y; R[o + 5u] = e.z;
}

//@pass pbr_shade
// Shades surface samples (raw::lighting::shadePunctual over the sample's cluster list, plus
// raw::lighting::shadeIbl), multiplied by the exposure. One invocation per sample.
@group(0) @binding(0) var<uniform> P: PbrParams;
@group(0) @binding(1) var<storage, read> C: array<f32>;
@group(0) @binding(2) var<storage, read> T: array<f32>;
@group(0) @binding(3) var<storage, read> L: array<f32>;
@group(0) @binding(4) var<storage, read> K: array<u32>;
@group(0) @binding(5) var<storage, read> E: array<f32>;
@group(0) @binding(6) var<storage, read_write> R: array<f32>;

// A sample: the 36 material floats, then position, normal, tangent and the unit view vector.
const SAMPLE: u32 = 48u;
const NCLUSTERS: u32 = 3456u;
const MAX_PER_CLUSTER: u32 = 256u;

fn env_texel(level: u32, f: u32, y: i32, x: i32) -> vec3f {
    let n: u32 = cube_level_size(P.env_size, level);
    let t: vec3u = cube_tap(n, f, y, x);
    let i: u32 = cube_level_offset(P.env_size, level) + ((t.x * n + t.y) * n + t.z) * 3u;
    return vec3f(E[i], E[i + 1u], E[i + 2u]);
}
fn env_sample(level: u32, d: vec3f) -> vec3f {
    let n: u32 = cube_level_size(P.env_size, level);
    let fu: vec3f = cube_face(d);
    let f: u32 = u32(fu.x);
    let fx: f32 = fu.y * f32(n) - 0.5;
    let fy: f32 = fu.z * f32(n) - 0.5;
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let ax: f32 = fx - f32(x0);
    let ay: f32 = fy - f32(y0);
    let a: vec3f = env_texel(level, f, y0, x0);
    let b: vec3f = env_texel(level, f, y0, x0 + 1);
    let c: vec3f = env_texel(level, f, y0 + 1, x0);
    let e: vec3f = env_texel(level, f, y0 + 1, x0 + 1);
    return (a * (1.0 - ax) + b * ax) * (1.0 - ay) + (c * (1.0 - ax) + e * ax) * ay;
}
fn env_rough(r: f32, d: vec3f) -> vec3f {
    let l: f32 = clamp(r, 0.0, 1.0) * f32(P.env_levels - 1u);
    let l0: u32 = min(u32(l), P.env_levels - 1u);
    let l1: u32 = min(l0 + 1u, P.env_levels - 1u);
    let f: f32 = l - f32(l0);
    return env_sample(l0, d) * (1.0 - f) + env_sample(l1, d) * f;
}
// Irradiance / pi from the order-2 harmonics stored at E[P.sh_off] (9 RGB, raw::lighting::shIrradiance).
fn sh_irr(n: vec3f) -> vec3f {
    var y: array<f32, 9>;
    y[0] = 0.282094791773878;
    y[1] = 0.488602511902920 * n.y; y[2] = 0.488602511902920 * n.z; y[3] = 0.488602511902920 * n.x;
    y[4] = 1.092548430592079 * n.x * n.y; y[5] = 1.092548430592079 * n.y * n.z;
    y[6] = 0.315391565252520 * (3.0 * n.z * n.z - 1.0);
    y[7] = 1.092548430592079 * n.x * n.z; y[8] = 0.546274215296040 * (n.x * n.x - n.y * n.y);
    var e: vec3f = vec3f(0.0);
    for (var k: u32 = 0u; k < 9u; k++) {
        let band: f32 = select(select(0.25, 2.0 / 3.0, k < 4u), 1.0, k == 0u);
        let o: u32 = P.sh_off + k * 3u;
        e = e + vec3f(E[o], E[o + 1u], E[o + 2u]) * (band * y[k]);
    }
    return max(e, vec3f(0.0));
}
// raw::lighting::illuminance: lux per channel arriving at p, and the unit direction to the light.
fn illum(i: u32, p: vec3f, to_light: ptr<function, vec3f>) -> vec3f {
    let b: u32 = i * 16u;
    let col: vec3f = vec3f(L[b + 7u], L[b + 8u], L[b + 9u]);
    if (L[b] == 0.0) {
        *to_light = -vec3f(L[b + 4u], L[b + 5u], L[b + 6u]);
        return col * L[b + 10u];
    }
    let d: vec3f = vec3f(L[b + 1u], L[b + 2u], L[b + 3u]) - p;
    let d2: f32 = dot(d, d);
    let dist: f32 = sqrt(d2);
    if (dist <= 0.0) { *to_light = vec3f(0.0, 0.0, 1.0); return vec3f(0.0); }
    *to_light = d / dist;
    var att: f32 = 1.0 / d2;
    let range: f32 = L[b + 11u];
    if (range > 0.0) {
        let q: f32 = dist / range;
        let w: f32 = clamp(1.0 - q * q * q * q, 0.0, 1.0);
        att = att * w * w;
    }
    if (L[b] == 2.0) {
        let cd: f32 = -dot(vec3f(L[b + 4u], L[b + 5u], L[b + 6u]), *to_light);
        let a: f32 = clamp(cd * L[b + 12u] + L[b + 13u], 0.0, 1.0);
        att = att * a * a;
    }
    return col * (L[b + 10u] * att);
}
// The lobe centroid's direction (raw::lighting dominant(), Tables::Dom).
fn dominant(n: vec3f, v: vec3f, rough: f32) -> vec3f {
    let nv: f32 = clamp(dot(n, v), 0.0, 1.0);
    let e: f32 = read2(OFF_DOM, N_DOM, N_DOM, nv, rough);
    let r: vec3f = 2.0 * nv * n - v;
    let p: vec3f = r - dot(r, n) * n;
    let pl: f32 = sqrt(dot(p, p));
    if (pl < 1e-12) { return n; }
    return n * cos(e) + p * (sin(e) / pl);
}
fn spec_w(b: u32, f0: vec3f, f90: f32, a: f32, bb: f32, mu: f32) -> vec3f {
    let w: vec3f = f0 * a + vec3f(f90 * bb);
    let iri: f32 = C[b + 28u];
    if (iri <= 0.0) { return w; }
    return w * (1.0 - iri) + irid_f(C[b + 29u], C[b + 30u], f0, mu, b) * ((a + bb) * iri);
}
// raw::pbr::iblResponse and raw::lighting::shadeIbl for one sample.
fn ibl(b: u32, wo: vec3f, n: vec3f, t: vec3f, v: vec3f) -> vec3f {
    let base: vec3f = cv3(b, 0u);
    let mt: f32 = clamp(C[b + 3u], 0.0, 1.0);
    let tr: f32 = clamp(C[b + 19u], 0.0, 1.0);
    let rr: f32 = clamp(C[b + 4u], MIN_ROUGH, 1.0);
    let ak: f32 = clamp(C[b + 26u], 0.0, 1.0);
    let abar: f32 = select(read1(OFF_AAVG, N_ROUGH, rr), read2rk(OFF_AAVG2, rr, ak), ak > 0.0);
    let bbar: f32 = select(read1(OFF_BAVG, N_ROUGH, rr), read2rk(OFF_BAVG2, rr, ak), ak > 0.0);
    let ebar: f32 = abar + bbar;
    let q: f32 = (C[b + 5u] - 1.0) / (C[b + 5u] + 1.0);
    let f90: f32 = C[b + 6u];
    let f0d: vec3f = min(q * q * cv3(b, 7u), vec3f(1.0)) * f90;
    let km: vec3f = kms3(base, 1.0, ebar);
    let kd: vec3f = kms3(f0d, f90, ebar);
    let cr: f32 = C[b + 27u];
    let sr: f32 = C[b + 35u];
    let o: vec3f = vec3f(cr * wo.x + sr * wo.y, -sr * wo.x + cr * wo.y, wo.z);
    let ab: vec2f = split_ab(o, rr, ak);
    let e: f32 = ab.x + ab.y;
    let es_o: vec3f = es_at(ab, f0d, f90, kd);
    var spec: vec3f = spec_w(b, base, 1.0, ab.x, ab.y, o.z) * mt + spec_w(b, f0d, f90, ab.x, ab.y, o.z) * (1.0 - mt);
    var under: vec3f = vec3f(1.0) - es_o;
    let iri: f32 = C[b + 28u];
    if (iri > 0.0) { under = under * (1.0 - iri) + vec3f((1.0 - pmax3(irid_f(C[b + 29u], C[b + 30u], f0d, o.z, b))) * iri); }
    var irr: vec3f = (km * mt + kd * (1.0 - mt)) * (1.0 - e) + base * under * ((1.0 - mt) * (1.0 - tr));
    var trans: vec3f = vec3f(0.0);
    if (tr > 0.0 && mt < 1.0) {
        var att: vec3f = vec3f(1.0);
        if (is_volume(b) && C[b + 22u] > 0.0 && C[b + 21u] > 0.0) {
            let ior_t: f32 = C[b + 5u];
            let cos_t: f32 = sqrt(max(1e-12, 1.0 - (1.0 - o.z * o.z) / (ior_t * ior_t)));
            let path: f32 = select(C[b + 21u] / cos_t, C[b + 21u], spec_exact(b));
            att = exp(log(max(cv3(b, 23u), vec3f(1e-30))) / C[b + 22u] * path);
        }
        trans = base * att * under * (tr * (1.0 - mt));
    }
    let shc: vec3f = cv3(b, 15u);
    let sh: f32 = pmax3(shc);
    if (sh > 0.0) {
        let e_sh: f32 = read_sh(wo.z, C[b + 18u]);
        let keep: f32 = 1.0 - sh * e_sh;
        spec = spec * keep; irr = irr * keep; trans = trans * keep;
        irr = irr + shc * e_sh;
    }
    let c: f32 = clamp(C[b + 10u], 0.0, 1.0);
    var coat: f32 = 0.0;
    let rc: f32 = clamp(C[b + 11u], MIN_ROUGH, 1.0);
    if (c > 0.0) {
        let mu: f32 = max(dot(pnorm(cv3(b, 12u)), wo), 0.0);
        let kc: f32 = kms(0.04 + 0.96 / 21.0, tab_eavg(rc));
        let ac: f32 = read2(OFF_A, N_THETA, N_ROUGH, mu, rc);
        let bc: f32 = read2(OFF_B, N_THETA, N_ROUGH, mu, rc);
        let ec: f32 = 0.04 * ac + bc + (1.0 - ac - bc) * kc;
        let keep: f32 = select((1.0 - c * ec) * (1.0 - c * ec), 1.0 - c * schlick(0.04, 1.0, mu), spec_exact(b));
        spec = spec * keep; irr = irr * keep; trans = trans * keep;
        coat = c * (0.04 * ac + bc);
        if (!spec_exact(b)) { irr = irr + vec3f(c * kc * (1.0 - ac - bc)); }
    }
    var acc: vec3f = spec * env_rough(rr, dominant(n, v, rr)) + irr * sh_irr(n);
    if (pmax3(trans) > 0.0) {
        var td: vec3f = -v;
        if (is_volume(b)) {
            let eta: f32 = 1.0 / C[b + 5u];
            let cs: f32 = dot(n, v);
            let k: f32 = 1.0 - eta * eta * (1.0 - cs * cs);
            td = select(eta * (-v) + (eta * cs - sqrt(max(k, 0.0))) * n, -v + 2.0 * cs * n, k < 0.0);
        }
        acc = acc + trans * env_rough(rr, td);
    }
    if (coat > 0.0) {
        let bt: vec3f = cross(n, t);
        let cl: vec3f = cv3(b, 12u);
        let cn: vec3f = pnorm(t * cl.x + bt * cl.y + n * cl.z);
        acc = acc + vec3f(coat) * env_rough(rc, dominant(cn, v, rc));
    }
    return acc;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let k: u32 = gid.x;
    if (k >= P.count) { return; }
    let b: u32 = k * SAMPLE;
    let p: vec3f = cv3(b, 36u);
    let n: vec3f = cv3(b, 39u);
    let t: vec3f = cv3(b, 42u);
    let v: vec3f = cv3(b, 45u);
    let bt: vec3f = cross(n, t);
    let wo: vec3f = vec3f(dot(v, t), dot(v, bt), dot(v, n));
    var sum: vec3f = cv3(b, 31u) * C[b + 34u];
    let c: i32 = cluster_of(p, P.grid);
    if (c >= 0) {
        let cnt: u32 = K[u32(c)];
        for (var j: u32 = 0u; j < cnt; j++) {
            let li: u32 = K[NCLUSTERS + u32(c) * MAX_PER_CLUSTER + j];
            var dl: vec3f = vec3f(0.0);
            let e: vec3f = illum(li, p, &dl);
            if (e.x == 0.0 && e.y == 0.0 && e.z == 0.0) { continue; }
            let wi: vec3f = vec3f(dot(dl, t), dot(dl, bt), dot(dl, n));
            sum = sum + eval_case(b, wo, wi) * e * abs(wi.z);
        }
    }
    let o: vec3f = (sum + ibl(b, wo, n, t, v)) * P.misc.x;
    let r: u32 = k * 3u;
    R[r] = o.x; R[r + 1u] = o.y; R[r + 2u] = o.z;
}
