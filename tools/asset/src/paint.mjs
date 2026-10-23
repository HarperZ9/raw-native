// Layered painting in 3D, the Oil and Ink way: colour is laid as zones, dabs and bands in the sculpt's own
// frame, then shadow lean and ink come from the baked cavity and curvature, then the brush grain. A paint
// file is text: layers in order, each a list of strokes. Evaluating a layer stack at a surface sample gives
// linear RGB, so the same file paints texels and mesh vertices and the two cannot drift.
export const PAINT_FORMAT = "rnpaint/1";
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t), sm = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a || 1e-9))); return t * t * (3 - 2 * t); };
const h3 = (x, y, z) => { let n = (Math.imul(x | 0, 73856093) ^ Math.imul(y | 0, 19349663) ^ Math.imul(z | 0, 83492791)) >>> 0; n = Math.imul(n ^ (n >>> 13), 1274126177); return ((n ^ (n >>> 16)) >>> 0) / 4294967296; };
export function vnoise(x, y, z) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z), fx = x - ix, fy = y - iy, fz = z - iz, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy), w = fz * fz * (3 - 2 * fz);
  const l = (a, b, t) => a + (b - a) * t, c = (dx, dy, dz) => h3(ix + dx, iy + dy, iz + dz);
  return l(l(l(c(0, 0, 0), c(1, 0, 0), u), l(c(0, 1, 0), c(1, 1, 0), u), v), l(l(c(0, 0, 1), c(1, 0, 1), u), l(c(0, 1, 1), c(1, 1, 1), u), v), w);
}

function weight(s, S) {
  const [x, y, z] = S.P, ax = Math.abs(x);
  switch (s.kind) {
    case "fill": return 1;
    case "ramp": { const st = s.stops, v = s.axis === "z" ? z : s.axis === "x" ? ax : y; if (v <= st[0][0]) return { c: st[0][1] }; for (let i = 1; i < st.length; i++) if (v <= st[i][0]) return { c: mix(st[i - 1][1], st[i][1], sm(st[i - 1][0], st[i][0], v)) }; return { c: st[st.length - 1][1] }; }
    case "dab": { const best = (s.mirror ? [s.c, [-s.c[0], s.c[1], s.c[2]]] : [s.c]).map((c) => Math.hypot(x - c[0], (y - c[1]) * (s.sy || 1), z - c[2])); const d = Math.min(...best); return 1 - sm(s.r * (s.hard ?? 0.3), s.r, d); }
    case "band": return sm(s.y0 - s.soft, s.y0, y) * (1 - sm(s.y1, s.y1 + s.soft, y)) * sm(s.zmin ?? -9, (s.zmin ?? -9) + 0.01, z);
    case "ink": { const v = s.src === "curv" ? -S.kappa / 60 : S.cav; return sm(s.lo, s.hi, v); }
    case "shade": return Math.pow(1 - S.ao, s.pow ?? 1) * (s.gain ?? 1);
    case "thin": return (1 - sm(0.004, 0.02, S.thick)) * (s.gain ?? 1);
    default: throw new Error(`paint: unknown stroke kind "${s.kind}"`);
  }
}

export function paintAt(P, S) {
  let c = [0.5, 0.5, 0.5];
  for (const L of P.layers) {
    if (L.region === "skin" && S.hair) continue; if (L.region === "hair" && !S.hair) continue;
    for (const s of L.strokes) {
      if (s.kind === "brush") {   // grain: value modulated by noise stretched along a flow direction
        const f = s.scale, a = s.aniso || [1, 1, 1], n = vnoise(S.P[0] * f * a[0], S.P[1] * f * a[1], S.P[2] * f * a[2]) * 0.65 + vnoise(S.P[0] * f * 2.7 * a[0], S.P[1] * f * 2.7 * a[1], S.P[2] * f * 2.7 * a[2]) * 0.35;
        c = c.map((v) => v * (1 + s.amp * (n - 0.5) * 2 * (L.opacity ?? 1))); continue;
      }
      const w0 = weight(s, S), w = (typeof w0 === "number" ? w0 : 1) * (L.opacity ?? 1) * (s.opacity ?? 1), col = typeof w0 === "number" ? s.color : w0.c;
      if (!(w > 0)) continue;
      c = (s.blend || L.blend || "over") === "mul" ? c.map((v, i) => v * (1 + (col[i] - 1) * w)) : (s.blend || L.blend) === "add" ? c.map((v, i) => v + col[i] * w) : mix(c, col, Math.min(1, w));
    }
  }
  return c.map((v) => Math.max(0, Math.min(1.5, v)));
}
export const parsePaint = (text) => { const P = JSON.parse(text); if (P.format !== PAINT_FORMAT) throw new Error(`rnpaint: format is "${P.format}"`); if (!Array.isArray(P.layers)) throw new Error("rnpaint: layers must be an array"); return P; };
export const toSrgb8 = (c) => c.map((v) => Math.round(255 * Math.max(0, Math.min(1, Math.pow(Math.max(0, v), 1 / 2.2)))));
