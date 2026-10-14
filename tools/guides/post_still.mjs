// Lay a separately rendered hud (labels, key terms) over a finished painted still:
//   node tools/guides/post_still.mjs art.png out.png --hud-black b.png --hud-white w.png
// The paint pass is a scene pass, so type drawn in the same list would be painted with the
// art. The hud is rendered alone on black and on white instead, and its exact coverage and
// colour are recovered from the pair. Every look itself (paint, film, tube, dither, pixel
// mode) is the engine's own post stack, applied in the render.
import { readPng, writePng } from "../shaders/png.mjs";

// The hud was rendered alone on black (B) and on white (Wh), so its coverage is
// a = 1 - (Wh - B) / 255 and its colour B / a, exactly, per channel.
export function overlayHud(art, B, Wh) {
  const o = new Uint8ClampedArray(art.data);
  for (let i = 0; i < o.length; i += 4) for (let c = 0; c < 3; c++) {
    const a = 1 - (Wh.data[i + c] - B.data[i + c]) / 255;
    if (a > 0.002) o[i + c] = Math.round(B.data[i + c] + (1 - a) * art.data[i + c]);
  }
  return { ...art, data: o };
}

const args = process.argv.slice(2), val = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : null; };
const [inp, out] = args.filter((a, i) => !a.startsWith("--") && !(args[i - 1] || "").startsWith("--"));
if (inp && out) {
  const b = val("--hud-black"), w = val("--hud-white");
  if (!b || !w) throw new Error("post_still: --hud-black and --hud-white are required");
  writePng(out, overlayHud(readPng(inp), readPng(b), readPng(w)));
  console.log(`${out}: hud laid over the painted art`);
}
