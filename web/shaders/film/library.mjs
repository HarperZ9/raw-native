// The measured film-stock library: data-sheet curves reduced to the film shader's numbers.
//
// FILM_LIBRARY (library-data.mjs, generated) holds, per stock, the logistic fit of each layer's
// characteristic curve in log lux-seconds, the 18% grey reference, the spectral-sensitivity peaks and
// half widths, dye peaks, the 50% MTF frequency, granularity and halation control, each with its source
// file, sha256, page and digitisation method. Stocks with no usable public sheet are listed with the
// reason and carry no curves.
//
// libraryNegative(id) turns a colour negative into the record stocks.mjs builds from. Where a sheet
// leaves a number out, the record takes the measured 500T value and lists it under `borrowed`, so a
// look built on it can say which parts are its own.
import { FILM_LIBRARY } from "./library-data.mjs";

export { FILM_LIBRARY };
const g = (c, fwhm) => { const s = fwhm / 2.3548; return (l) => Math.exp(-0.5 * ((l - c) / s) ** 2); };
const g2 = (c, left, right) => { const sl = left / 1.17741, sr = right / 1.17741; return (l) => Math.exp(-0.5 * ((l - c) / (l < c ? sl : sr)) ** 2); };
const LAYERS = ["R", "G", "B"];   // channel 0 red-sensitive (cyan dye), 1 green, 2 blue

// Halation control behind the emulsion. Light that reaches the base and comes back exposes the emulsion
// again (the ring of crt/halo.mjs at 2 t tan(theta_c)); what stops it decides how much returns.
//   none     no antihalation layer (CineStill's rem-jet removal): the full total-internal-reflection return
//   remjet   carbon-black backing on the far side of the base: the return off the back is absorbed; 0.03 of
//            it stays (the value the existing 500T presets use; low confidence)
//   ahu      an antihalation undercoat between emulsion and base, crossed twice: (10^-OD)^2 with an optical
//            density of 0.6 per pass, 0.063 (from memory, low confidence: an input, not a claim)
export const HALATION = { none: 1, remjet: 0.03, ahu: Math.pow(10, -2 * 0.6) };

export function stockIds(kind) { return Object.keys(FILM_LIBRARY.stocks).filter((k) => !kind || FILM_LIBRARY.stocks[k].kind === kind); }

// The halation control a stock's current edition states: Kodak's 3-26 VISION3 editions replace the
// rem-jet with an undercoat; still films with an antihalation layer are treated as an undercoat.
export function statedHalation(id) {
  const h = FILM_LIBRARY.stocks[id]?.halation;
  if (!h) return null;
  // the current edition wins: a held edition that names an undercoat is newer than one that names rem-jet
  const eds = Object.values(h.byEdition || {});
  const s = eds.find((v) => v.includes("undercoat")) || h.stated || "";
  // "limited antihalation" (HARMAN PHOENIX) has no stated density: treated as none, the upper bound on the return.
  if (s.includes("limited")) return "none";
  return s.includes("undercoat") || s.includes("antihalation") ? "ahu" : s.includes("rem-jet") ? "remjet" : null;
}

const sigmaFromF50 = (f50) => (f50 ? (1000 * Math.sqrt(Math.LN2 / 2)) / (Math.PI * f50) : null);   // um, gaussian scatter

export function libraryNegative(id, fallback) {
  const s = FILM_LIBRARY.stocks[id];
  if (!s) throw new Error(`unknown film stock ${id}`);
  if (s.kind !== "colour-negative") throw new Error(`${id} is a ${s.kind}; the film shader renders colour negatives through a print (reversal and black-and-white stocks are library data only)`);
  if (!s.sensitometry || !s.grey) throw new Error(`${id}: no characteristic curves in the library (${s.prov.why || s.missing.join(", ")})`);
  const borrowed = [];
  const f = LAYERS.map((k) => s.sensitometry.fit[k]);
  const off = Math.log10(0.18) - s.grey.logH;          // log lux-seconds -> the shader's log exposure (18% grey = log10 0.18)
  const sens = s.spectral && LAYERS.every((k) => s.spectral[k]?.hwhmL && s.spectral[k]?.hwhmR)
    ? LAYERS.map((k) => g2(s.spectral[k].peak, s.spectral[k].hwhmL, s.spectral[k].hwhmR))
    : (borrowed.push("spectral sensitivity"), fallback.sens);
  const dy = s.dyes && s.dyes.cyan && s.dyes.magenta && s.dyes.yellow;
  const prim = dy ? [g(s.dyes.cyan, 100), g(s.dyes.magenta, 85), g(s.dyes.yellow, 90)] : (borrowed.push("dye spectra"), fallback.dyes.prim);
  borrowed.push("dye masking (secondary absorptions)", "interimage gain");
  const sigmaD48 = s.sigmaD48 ? LAYERS.map((k) => s.sigmaD48[k]) : s.diffuseRms ? [0, 1, 2].map(() => s.diffuseRms / 1000) : (borrowed.push("granularity"), fallback.sigmaD48);
  const emulsionSigmaUm = s.mtf50 && LAYERS.every((k) => s.mtf50[k]) ? LAYERS.map((k) => sigmaFromF50(s.mtf50[k])) : (borrowed.push("MTF"), null);
  return {
    sens, prim, sigmaD48, emulsionSigmaUm, borrowed,
    // The measured span and slope; D-min stays the shader's 0.05 because the measured Status M D-min holds the
    // orange mask, which the shader carries in its integral masking term instead.
    gamma: f.map((v) => (v.k * v.span) / 4), span: f.map((v) => v.span), k: f.map((v) => v.k), x0: f.map((v) => v.x0 + off),
    dmin: [0.05, 0.05, 0.05], measuredDmin: f.map((v) => v.dmin), grey: s.grey, prov: s.prov,
  };
}
