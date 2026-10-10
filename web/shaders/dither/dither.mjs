// Palette quantisation and dither: the CPU reference (the author's retro-dither.js, vendored
// unchanged, plus a blue-noise mode on its orderedPick) and the GPU runner.
//   const q = await createGpuDither(host, { palette: "pico8", mode: "bayer8", strength: 1 }, { w, h });
//   q.frame(rgba8 /* Uint8Array of the plate */);  await q.read() -> { idx: Uint32Array, rgba: Uint8Array }
import { ditherPlate, orderedPick } from "../reference/retro-dither.mjs";
import { labPalette, nearestIndex, SRGB8_TO_LINEAR, linearToOklab } from "../reference/retro-palettes.mjs";
import { blueNoise64 } from "./bluenoise.mjs";
import { dsSplit, DS_WGSL } from "./ds.wgsl.mjs";
import { DITHER_WGSL } from "./dither.wgsl.mjs";

export const MODES = { none: 0, bayer2: 1, bayer4: 2, bayer8: 3, noise: 4, blue: 5 };
const clamp01 = (x) => (x < 0 ? 0 : x > 1 ? 1 : x);

// Plate bytes to OKLab, as retro-engine's quantizeGrid does with gamma 1.
export function plateLab(bytes, w, h, brightness = 0) {
  const lab = new Array(w * h);
  for (let p = 0; p < w * h; p++) {
    const i = p * 4, [L, a, b] = linearToOklab(SRGB8_TO_LINEAR[bytes[i]], SRGB8_TO_LINEAR[bytes[i + 1]], SRGB8_TO_LINEAR[bytes[i + 2]]);
    lab[p] = [clamp01(L + brightness), a, b];
  }
  return lab;
}

// CPU reference: palette indices for a plate.
export function ditherRef(bytes, w, h, { palette = "pico8", mode = "bayer4", strength = 1, brightness = 0 } = {}) {
  const lab = plateLab(bytes, w, h, brightness), pal = typeof palette === "string" ? labPalette(palette) : palette;
  if (mode !== "blue") return ditherPlate(lab, w, h, pal, mode, strength);
  const s = clamp01(+strength || 0), bn = blueNoise64(), out = new Uint16Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const p = y * w + x, c = lab[p];
    out[p] = s > 0 ? orderedPick(c[0], c[1], c[2], pal, (bn[(y % 64) * 64 + (x % 64)] + 0.5) / 4096, s) : nearestIndex(c[0], c[1], c[2], pal);
  }
  return out;
}

// Constants buffer: sRGB-to-linear table, the two OKLab matrices, epsilon, the pair penalty,
// the IGN constants, brightness and strength, each as a (hi, lo) f32 pair.
const M1 = [0.4122214708, 0.5363325363, 0.0514459929, 0.2119034982, 0.6806995451, 0.1073969566, 0.0883024619, 0.2817188376, 0.6299787005];
const M2 = [0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050, 0.4505937099, 0.0259040371, 0.7827717662, -0.8086757660];
export function packConstants(brightness, strength) {
  const v = [...SRGB8_TO_LINEAR, ...M1, ...M2, 1e-9, 0.12, 0.06711056, 0.00583715, 52.9829189, brightness, clamp01(+strength || 0)];
  return Float32Array.from(v.flatMap(dsSplit));
}
export function packPalette(pal) {
  return { lab: Float32Array.from(pal.flatMap((e) => e.lab.flatMap(dsSplit))),
    rgb: Uint32Array.from(pal.map((e) => (e.rgb[0] | (e.rgb[1] << 8) | (e.rgb[2] << 16) | (255 << 24)) >>> 0)) };
}
export const DITHER_PASS = DS_WGSL + DITHER_WGSL;

export async function createGpuDither(host, { palette = "pico8", mode = "bayer4", strength = 1, brightness = 0 } = {}, size) {
  const pal = typeof palette === "string" ? labPalette(palette) : palette, pp = packPalette(pal), n = size.w * size.h;
  const pipe = await host.compute("dither", DITHER_PASS);
  const st = (bytes, label, data) => host.buffer({ size: bytes, usage: ["storage", "copy-dst", "copy-src"], label, data });
  const B = {
    P: st(16, "dither params", new Float32Array([size.w, size.h, pal.length, MODES[mode]])),
    K: st(4 * 2 * 281, "dither constants", packConstants(brightness, strength)),
    pal: st(pp.lab.byteLength, "dither palette", pp.lab), palrgb: st(pp.rgb.byteLength, "dither palette rgb", pp.rgb),
    bn: st(4096 * 4, "blue noise", Uint32Array.from(blueNoise64())),
    src: st(4 * n, "dither plate"), idx: st(4 * n, "dither indices"), out8: st(4 * n, "dither rgba"),
  };
  const list = [B.P, B.K, B.pal, B.palrgb, B.bn, B.src, B.idx, B.out8];
  return {
    buffers: B, packed: B.out8, palette: pal,
    frame(rgba8) {
      if (rgba8) host.write(B.src, rgba8);
      const enc = host.device.createCommandEncoder(), cp = enc.beginComputePass();
      cp.setPipeline(pipe); cp.setBindGroup(0, host.bind(pipe, list)); cp.dispatchWorkgroups(Math.ceil(size.w / 8), Math.ceil(size.h / 8)); cp.end();
      host.device.queue.submit([enc.finish()]);
    },
    async read() {
      const rd = async (buf) => {
        const s = host.buffer({ size: 4 * n, usage: ["map-read", "copy-dst"] }), enc = host.device.createCommandEncoder();
        enc.copyBufferToBuffer(buf, 0, s, 0, 4 * n); host.device.queue.submit([enc.finish()]);
        await s.mapAsync(1); const o = s.getMappedRange().slice(0); s.unmap(); s.destroy(); return o;
      };
      return { idx: new Uint32Array(await rd(B.idx)), rgba: new Uint8Array(await rd(B.out8)) };
    },
    destroy() { for (const b of Object.values(B)) b.destroy(); },
  };
}
