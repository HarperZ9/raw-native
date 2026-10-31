// Vector (XY, stroke) display mode, CPU reference.
//
// The beam is steered straight along each segment of a display list instead of scanning lines. The
// light a stretch of phosphor receives is the beam current times the time the spot spends over it, so a
// segment drawn slowly is brighter than one drawn fast, and the beam's settle time at each segment end
// leaves the bright vertex dots vector games are known for. The beam slews at a fixed speed in mm/us
// (the deflection amplifier's limit); a segment's duration is its length over that speed, and every
// endpoint adds a settle dwell. The deposit is a round gaussian spot integrated over the output pixel
// (erf, as the raster beam pass does). Persistence uses the screen's two-exponential decay per frame,
// so a moving figure leaves the trail its phosphor gives it.
//
// Parameters come from the "vector" option record (options.mjs crt.mode): slew, settle and spot are
// recalled orders of magnitude with status from_memory, inputs to vary rather than claims about one monitor.
import { gaussMass } from "../common.mjs";
import { screenMatrix, decayTable } from "./phosphors.mjs";
import { option } from "../options.mjs";

export function resolveVector({ widthMm = 300, heightMm = 225, screen = "p31", frameS = 1 / 60, ...o } = {}, out) {
  const rec = option("crt.mode", "vector"), d = rec.parameters;
  const p = { slew: d.slew_mm_per_us, settle: d.settle_us, sigma: d.spot_sigma_mm, ...o };
  const scr = screenMatrix(screen, { output: "rec709" });
  return { p, out, widthMm, heightMm, mmPerPx: widthMm / out.w, screen: scr, decay: decayTable(scr.parts, frameS), frameS, prov: rec.prov };
}

// Light per output pixel from one display list. segs: [{ x0, y0, x1, y1, i, rgb }] in mm about the centre,
// i the beam current (0..1), rgb optional gun drive for a colour screen. Returns gun light (w x h x 3).
export function strokeLight(plan, segs) {
  const { out, mmPerPx: s, p } = plan, light = new Float64Array(out.w * out.h * 3);
  const deposit = (cx, cy, dwellUs, gun) => {
    const r = 3 * p.sigma, x0 = Math.max(0, Math.floor((cx - r) / s + out.w / 2)), x1 = Math.min(out.w - 1, Math.ceil((cx + r) / s + out.w / 2));
    const y0 = Math.max(0, Math.floor((cy - r) / s + out.h / 2)), y1 = Math.min(out.h - 1, Math.ceil((cy + r) / s + out.h / 2));
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      const ax = (x - out.w / 2) * s, ay = (y - out.h / 2) * s;
      const m = gaussMass(ax, ax + s, cx, p.sigma) * gaussMass(ay, ay + s, cy, p.sigma);
      for (let c = 0; c < 3; c++) light[(y * out.w + x) * 3 + c] += m * dwellUs * gun[c];
    }
  };
  for (const g of segs) {
    const gun = (g.rgb || [1, 1, 1]).map((v) => v * (g.i ?? 1));
    const L = Math.hypot(g.x1 - g.x0, g.y1 - g.y0), T = L / p.slew;
    const n = Math.max(1, Math.ceil(L / (0.25 * p.sigma)));        // samples a quarter spot apart
    for (let k = 0; k < n; k++) {
      const t = (k + 0.5) / n;
      deposit(g.x0 + (g.x1 - g.x0) * t, g.y0 + (g.y1 - g.y0) * t, T / n, gun);
    }
    deposit(g.x0, g.y0, p.settle, gun);                              // the beam settles at each end
    deposit(g.x1, g.y1, p.settle, gun);
  }
  return light;
}

// One frame: gun light through the phosphor decay (two terms per component, carried in hist) and the
// screen matrix to linear Rec.709. hist: Float64Array(w * h * 6), zero at the start.
export function vectorFrame(plan, segs, hist) {
  const light = strokeLight(plan, segs), { out, decay, screen } = plan, img = new Float32Array(out.w * out.h * 4);
  const parts = screen.parts.length, m = screen.matrix;
  for (let i = 0; i < out.w * out.h; i++) {
    const comp = [0, 0, 0];
    for (let c = 0; c < Math.min(3, parts); c++) {
      const [w, e1, e2] = decay[c], gunIdx = screen.mono ? 0 : c, src = light[i * 3 + gunIdx] * (screen.mono ? (screen.parts[c].share ?? 1) : 1);
      const h1 = hist[i * 6 + 2 * c] * e1 + w * src, h2 = hist[i * 6 + 2 * c + 1] * e2 + (1 - w) * src;
      hist[i * 6 + 2 * c] = h1; hist[i * 6 + 2 * c + 1] = h2;
      comp[c] = h1 + h2;
    }
    for (let r = 0; r < 3; r++) img[i * 4 + r] = m[r * 3] * comp[0] + m[r * 3 + 1] * comp[1] + m[r * 3 + 2] * comp[2];
    img[i * 4 + 3] = 1;
  }
  return img;
}
