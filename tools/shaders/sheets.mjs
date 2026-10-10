// Comparison images for the shader library, from the CPU references (the GPU passes match
// them within one 8-bit code; see evidence/shaders-*-parity-bounds.json).
//   node tools/shaders/sheets.mjs <outdir> [--retro-crt <path to the site's system/retro-crt.js>]
// Writes one PNG per panel, all from the same source frame at the same output size.
import { mkdirSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { writePng, crop, nearest } from "./png.mjs";
import { createCrt, encode8 } from "../../web/shaders/crt/crt.mjs";
import { createFilm, encodeFilm8 } from "../../web/shaders/film/run.mjs";
import { testCard } from "../../web/shaders/fixtures/testcard.mjs";
import { sceneCard } from "../../web/shaders/fixtures/scene.mjs";
import { linearToSrgb } from "../../web/shaders/common.mjs";

const args = process.argv.slice(2), out = args[0] || "build/shader-sheets", ri = args.indexOf("--retro-crt");
mkdirSync(out, { recursive: true });
const save = (name, img) => { writePng(`${out}/${name}.png`, img); console.log(`${out}/${name}.png`); };
const W = 1024, H = 896, CROP = [380, 330, 256, 192];

function crtPanels() {
  const src = testCard(256, 224), bytes = new Uint8ClampedArray(src.data.length);
  for (let i = 0; i < bytes.length; i++) bytes[i] = Math.round(src.data[i] * 255);
  const up = nearest({ width: 256, height: 224, data: bytes }, 4);
  save("crt-0-source-nearest", up); save("crt-0-source-nearest-crop", nearest(crop(up, ...CROP), 4));
  for (const [name, preset, ov] of [["crt-2-raw-native-pvm-rgb", "pvm-20", {}], ["crt-3-raw-native-slot-composite", "slot-tv", {}],
    ["crt-4-raw-native-trinitron-composite", "trinitron-tv", {}], ["crt-5-raw-native-vga-delta", "vga-14", {}]]) {
    const crt = createCrt(preset, ov, { w: 256, h: 224 }, { w: W, h: H });
    let r; for (let f = 0; f < 2; f++) r = crt.frame(src, f);
    const img = { width: W, height: H, data: encode8(r.img, W, H, 1) };
    save(name, img); save(name + "-crop", nearest(crop(img, ...CROP), 4));
  }
}

async function retroPanel(path) {
  const { crtStage } = await import(pathToFileURL(path).href);
  const src = testCard(256, 224), bytes = new Uint8ClampedArray(src.data.length);
  for (let i = 0; i < bytes.length; i++) bytes[i] = Math.round(src.data[i] * 255);
  const img = nearest({ width: 256, height: 224, data: bytes }, 4);
  const ctx = { getImageData: () => ({ data: img.data }), putImageData: () => {} };
  // The Studio's defaults (re-* sliders), with the grille mask at its default strength.
  crtStage(ctx, img.width, img.height, { cell: 4, scanlines: true, scanStrength: 0.35, beam: 0.5, mask: "grille", maskStrength: 0.3,
    bloom: 0.18, halation: 0.2, curvature: 0.12, aberration: 0.1, vignette: 0.25 });
  save("crt-1-retro-crt-studio-defaults", img); save("crt-1-retro-crt-studio-defaults-crop", nearest(crop(img, ...CROP), 4));
}

function filmPanels() {
  const FW = 960, FH = 540, scene = sceneCard(FW, FH), plain = new Uint8ClampedArray(FW * FH * 4);
  for (let i = 0; i < plain.length; i++) plain[i] = (i & 3) === 3 ? 255 : Math.round(linearToSrgb(scene.data[i]) * 255);
  save("film-0-scene-plain-srgb", { width: FW, height: FH, data: plain });
  for (const [name, preset, ov] of [["film-1-500t-print", "500t-print", {}], ["film-2-500t-no-remjet", "500t-no-remjet", {}],
    ["film-3-bleach-bypass", "bleach-bypass", {}], ["film-4-250d-print", "250d-print", {}]]) {
    const film = createFilm(preset, ov, { w: FW, h: FH }, { w: FW, h: FH }), r = film.frame(scene, 0);
    save(name, { width: FW, height: FH, data: encodeFilm8(r.img, FW, FH) });
  }
}

crtPanels();
filmPanels();
if (ri >= 0) await retroPanel(args[ri + 1]);
