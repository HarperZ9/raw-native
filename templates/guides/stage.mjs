// Stages for the explainer guides: each guide posed inside the three style templates of
// ADR 0016 (the template's own tile map and look), and on a turnaround sheet. A shot is a
// Motion scene; tools/guides/render_all.py renders each one as a still on the software
// adapter. The painted look runs afterwards on the paint shader's CPU reference
// (tools/guides/paint_still.mjs), because its GPU pass is not registered yet (PR #55).
import { pose, lin, rad, INK } from "./rig.mjs";
import { rect } from "../../web/motion/path.mjs";
import { LOOKS, label } from "./look.mjs";
import thriller from "../retro-thriller/scene.mjs";
import painted from "../painted-iso-rpg/scene.mjs";
import pixel from "../pixel-2.5d/scene.mjs";
import { GUIDES } from "./guides.mjs";
import { thrillerSet } from "./sets.mjs";

const TEMPLATES = { thriller, painted, pixel };
const ATLAS = new URL("../../docs/motion/hanken-grotesk-500.atlas.json", import.meta.url).href;

let atlasP = null;
const atlas = () => (atlasP = atlasP || fetch(ATLAS).then((r) => r.json()));

async function loadAll(ctx) {
  const A = { atlas: await atlas() };
  for (const [k, tm] of Object.entries(TEMPLATES)) A[k] = await tm.load(ctx);
  for (const g of Object.values(GUIDES)) if (g.textures && ctx.motion) for (const [k, v] of Object.entries(g.textures())) ctx.motion.image(k, v);
  return A;
}

// The template's world items at time t, with its own camera, without its post.
// The thriller guides stand in a front-on set (sets.mjs) under the template's look; the
// painted and pixel guides stand on the template's own tile map.
export function world(style, A, t) {
  if (style === "thriller") return { items: thrillerSet(A), camera: { x: 960, y: 540, zoom: 1 } };
  const f = TEMPLATES[style].frame(t, { assets: A[style], params: {} });
  return { items: style === "thriller" ? f.items.slice(0, 1) : f.items, camera: f.camera };
}

// A turnaround (four views) over four poses, labelled, on the home style's ground.
function sheetFrame(g, style, A) {
  const L = LOOKS[style], items = [], hud = [], s = g.sheetScale;
  const views = [["front", 0], ["three-quarter", 35], ["side", 90], ["back", 180]];
  views.forEach(([name, yaw], i) => {
    const x = 270 + i * 460, y = 560;
    items.push(...g.under(x, y, s), ...pose(g.parts(g.rest), { x, y, s, yaw, shear: g.shear || 0 }));
    hud.push(label(A.atlas, name, 24, x, y + 34, L.dim));
  });
  g.poses.forEach((p, i) => {
    const x = 270 + i * 460, y = 1015;
    items.push(...g.under(x, y, s), ...pose(g.parts(p.o), { x, y, s, yaw: p.yaw || 0, flip: p.flip, shear: g.shear || 0 }));
    if (p.extra) items.push(...p.extra(x, y, s));
    hud.push(label(A.atlas, p.name, 24, x, y + 34, L.dim));
  });
  hud.push(label(A.atlas, g.title, 38, 60, 60, L.ink, "left"));
  hud.push(label(A.atlas, g.subtitle, 22, 60, 94, L.dim, "left"));
  if (style === "thriller") items.unshift({ shape: rect(0, 0, 1920, 1080), screen: true, fill: rad(960, 420, 1100, [0, [0.07, 0.06, 0.06, 1]], [1, [0.01, 0.01, 0.012, 1]]) });
  return { background: L.ground, items, hud, post: L.post };
}

// The guide inside a template's world, in that template's look, with the scene's props.
function sceneFrame(g, style, A, beat) {
  const L = LOOKS[style], w = world(style, A, beat.t ?? 0), cam = { ...w.camera, ...(beat.camera || {}) };
  const items = [...w.items];
  if (beat.props) items.push(...beat.props(style, A, cam));
  items.push(...g.under(beat.x, beat.y, beat.s, style), ...pose(g.parts(beat.o), { x: beat.x, y: beat.y, s: beat.s, yaw: beat.yaw || 0, flip: beat.flip, shear: g.shear || 0 }));
  if (beat.front) items.push(...beat.front(style, A, cam));
  if (g.light) items.push(...g.light(style, cam, beat));
  const hud = beat.hud ? beat.hud(style, A, L) : [];
  return { background: L.ground, camera: cam, items, hud, post: L.post };
}

// Labels and key terms (the hud) go on top of the art. A painted shot renders them apart,
// because the paint pass would smear the type: layer "art" is the frame without them, and
// "hud-black" and "hud-white" are the hud alone on black and on white, from which
// tools/guides/post_still.mjs recovers its exact coverage and lays it over the painted art.
function layered(f, layer) {
  if (layer === "hud-black" || layer === "hud-white") {
    const k = layer === "hud-white" ? 1 : 0;
    return { background: [k, k, k], items: f.hud, post: { bloom: 0, vignette: 0, grain: 0, passes: [] } };
  }
  return { ...f, items: layer === "art" ? f.items : [...f.items, ...f.hud] };
}

// shot({ guide, style, kind, beat, layer }) -> a Motion scene module.
export function shot({ guide, style, kind = "scene", beat = null, title, layer = null }) {
  const g = GUIDES[guide];
  return {
    title: title || `${g.title}: ${kind} (${style})`, duration: 1, fps: 30,
    async load(ctx) { return loadAll(ctx); },
    frame(t, ctx) {
      const A = ctx.assets;
      const f = kind === "sheet" ? sheetFrame(g, style, A) : sceneFrame(g, style, A, beat || g.beats[style]);
      return layered(f, layer || (style === "painted" ? "art" : null));
    },
  };
}
export { INK, lin };
