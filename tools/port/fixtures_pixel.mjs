// Fixtures of perspective-stable pixel art (evidence/cpp-port-style-bounds-10.json), each case's
// frame sequence run through createPixel as web/test/shaders-pixel.html runs it.
import { join } from "node:path";
import { createPixel, encodePixel8 } from "../../web/shaders/pixel/pixel.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

const view = (f) => { const r = f.radius || 6.5, a = f.angle; return { eye: [r * Math.sin(a), 4.2 * r / 6.5, r * Math.cos(a)], target: [f.targetX || 0, 0.6, 0], fovy: 0.75 }; };
export function pixel(out) {
  const b = readJson("evidence/shaders-pixel-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("pixel");
  B.head.cases = [];
  for (const c of b.cases) {
    const cpu = createPixel({ N: c.N }, { w: W, h: H });
    let ref = null;
    for (const f of c.frames) ref = cpu.frame(view(f));
    const id = c.id, pr = ref.probe;
    B.add(`${id}.img`, "f32", ref.img); B.add(`${id}.js`, "u8", encodePixel8(ref.img, W, H));
    B.add(`${id}.depth`, "f32", pr.depth); B.add(`${id}.mat`, "u32", Uint32Array.from(pr.mat, (v) => v >>> 0)); B.add(`${id}.obj`, "u32", Uint32Array.from(pr.obj, (v) => v >>> 0));
    B.add(`${id}.idb`, "u32", ref.vis.idb); B.add(`${id}.zbuf`, "f32", ref.vis.zbuf);
    B.head.cases.push({ id, N: c.N, width: W, height: H, views: c.frames.map(view), blend: ref.blend, origin: ref.origin });
  }
  B.write(join(out, "pixel.rnfx"));
  return B.head.cases.length;
}
