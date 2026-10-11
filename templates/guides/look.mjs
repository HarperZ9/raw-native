// Looks and light for the guide stages: each template's ground, label ink and post stack,
// and the overlays that light a guide inside a template world.
import { rad } from "./rig.mjs";
import { rect, circle } from "../../web/motion/path.mjs";
import { text } from "../../web/motion/text.mjs";

// Looks per style: ground colour, ink for labels, and the template's post stack.
export const LOOKS = {
  thriller: { ground: [0.012, 0.011, 0.014], ink: "#d9cdb4", dim: "#8a8070",
    post: { bloom: 0.2, threshold: 1.0, vignette: 0.2, grain: 0, passes: [] }, node: "film" },
  painted: { ground: [0.2, 0.15, 0.11], ink: "#f1e4c8", dim: "#b29d7e", post: { bloom: 0.08, vignette: 0.25, grain: 0, passes: [] }, node: "paint" },
  pixel: { ground: [0.02, 0.02, 0.05], ink: "#fff1e8", dim: "#c2c3c7", post: { bloom: 0, vignette: 0.1, grain: 0, passes: [{ pass: "dither", palette: "pico8", mode: "bayer4" }] },
  },
};

export const label = (A, s, size, x, y, fill, anchor = "center") => ({ shape: text(s, { atlas: A, size, x, y, anchor }).shape, fill, screen: true });

// A dark overlay with a hole of light at (lx, ly) on screen: the practical-light model of the
// thriller template. Everything outside the guide's light falls away.
export function darkness(lx, ly, r, k = 0.9) {
  return { shape: rect(-10, -10, 1940, 1100), screen: true,
    fill: rad(lx, ly, r, [0, [0, 0, 0, 0]], [0.35, [0, 0, 0, 0.12 * k]], [0.7, [0, 0, 0, 0.62 * k]], [1, [0, 0, 0, k]]) };
}
export const glow = (x, y, r, c = [1, 0.62, 0.28], a = 0.5) =>
  ({ shape: circle(x, y, r, 96), screen: true, blend: "add", fill: rad(x, y, r, [0, [...c, a]], [0.4, [...c, a * 0.35]], [1, [...c, 0]]) });

// Screen position of a scene point under a camera (Motion's mapping at depth 0).
export const toScreen = (cam, x, y) => [960 + (x - cam.x) * cam.zoom, 540 + (y - cam.y) * cam.zoom];
