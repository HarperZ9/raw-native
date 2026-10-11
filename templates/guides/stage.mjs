// Stages for the explainer guides: each guide posed inside the three style worlds of ADR
// 0016, and on a turnaround sheet. The worlds are hand-made: the thriller's night desk
// (sets.mjs), the painted harbour claims office (worlds/harbour.mjs) and the pixel tidepool
// (worlds/tidepool.mjs). A shot is a Motion scene with the style's own post stack (look.mjs);
// tools/guides/render_all.py renders each one as a still on the software adapter.
import { pose, lin, rad, INK } from "./rig.mjs";
import { rect } from "../../web/motion/path.mjs";
import { LOOKS, label } from "./look.mjs";
import { GUIDES } from "./guides.mjs";
import { thrillerSet } from "./sets.mjs";
import { loadTidepool, tidepool, REF } from "./worlds/tidepool.mjs";
import { loadHarbour, harbour, HARBOUR_CAMERA, LAMP } from "./worlds/harbour.mjs";
import { S as isoS, depth as isoDepth, sorted, castShadow } from "./worlds/iso_kit.mjs";
import { clipBeat, framing, CLIPS } from "./motion.mjs";
const ATLAS = new URL("../../docs/motion/hanken-grotesk-500.atlas.json", import.meta.url).href;

let atlasP = null;
const atlas = () => (atlasP = atlasP || fetch(ATLAS).then((r) => r.json()));

async function loadAll(ctx) {
  const A = { atlas: await atlas() };
  A.pixel = await loadTidepool(ctx.motion);
  A.painted = await loadHarbour(ctx.motion);
  A.painted.atlas = A.atlas;
  for (const g of Object.values(GUIDES)) if (g.textures && ctx.motion) for (const [k, v] of Object.entries(g.textures())) ctx.motion.image(k, v);
  return A;
}

// The world behind a guide and the part of it in front, with the world's own camera. In the
// painted harbour, things that stand on the ground (the guide among them, passed in `extra`
// as { d, items }) are drawn in depth order with the buildings and props.
export function world(style, A, extra = [], opts = {}) {
  if (style === "thriller") return { items: [...thrillerSet(A), ...extra.flatMap((t) => t.items)], front: [], camera: { x: 960, y: 540, zoom: 1 } };
  if (style === "pixel") { const w = tidepool(A.pixel, { front: !opts.noFront }); return { items: [...w.back, ...extra.flatMap((t) => t.items)], front: w.front, camera: { ...REF, zoom: 1 } }; }
  const w = harbour(A.painted, extra);
  return { items: [...w.back, ...sorted(w.things)], front: w.front, camera: { ...HARBOUR_CAMERA } };
}
// Pixel mode for the pixel style: the frame renders at the low resolution and scales by an integer.
const pixelOf = (style, sheet) => (style === "pixel" ? { pixel: { width: sheet ? 960 : LOOKS.pixel.pixelSize[0], height: sheet ? 540 : LOOKS.pixel.pixelSize[1], mode: "integer" } } : {});

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
  return { background: L.ground, items, hud, post: L.post, ...pixelOf(style, true) };
}

// The guide inside a template's world, in that template's look, with the scene's props.
function sceneFrame(g, style, A, beat) {
  const L = LOOKS[style];
  let { x, y } = beat;
  if (beat.cell) [x, y] = isoS(...beat.cell);
  if (beat.offset) { x += beat.offset[0]; y += beat.offset[1]; }
  const guide = [...g.under(x, y, beat.s, style), ...pose(g.parts(beat.o), { x, y, s: beat.s, yaw: beat.yaw || 0, flip: beat.flip, shear: g.shear || 0 })];
  const extra = [];
  if (beat.cell) {
    // In the harbour the guide is a thing on the ground: a long shadow away from the lamp, then the figure.
    extra.push({ d: isoDepth(...beat.cell), items: [...castShadow(...beat.cell, LAMP, beat.height || 3), ...guide] });
    if (beat.things) extra.push(...beat.things(A));
  } else {
    if (beat.props) extra.push({ d: 0, items: beat.props(style, A) });
    extra.push({ d: 1, items: guide });
  }
  const w = world(style, A, extra, { noFront: beat.noFront }), cam = { ...w.camera, ...(beat.camera || {}) };
  const items = [...w.items];
  if (beat.front) items.push(...beat.front(style, A, cam));
  if (g.light) items.push(...g.light(style, cam, { ...beat, x, y }));
  items.push(...w.front);
  const hud = beat.hud ? beat.hud(style, A, L) : [];
  return { background: L.ground, camera: cam, items, hud, post: L.post, ...pixelOf(style, false), ...(beat.pixel ? { pixel: beat.pixel } : {}) };
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

// shot({ guide, style, kind, beat, layer, clip, frame }) -> a Motion scene module. clip is
// "idle", "talk" or "action" (motion.mjs) and makes the scene move; frame is "wide",
// "medium" or "close".
export function shot({ guide, style, kind = "scene", beat = null, title, layer = null, clip = null, frame = null }) {
  const g = GUIDES[guide];
  return {
    title: title || `${g.title}: ${kind} (${style})`, duration: clip ? CLIPS[clip] : 1, fps: 30,
    async load(ctx) { return loadAll(ctx); },
    frame(t, ctx) {
      const A = ctx.assets;
      let b = beat || g.beats[style];
      if (kind !== "sheet") {
        if (clip) b = clipBeat(guide, clip, b, t);
        const base = world(style, A).camera;
        b = { ...b, ...framing(guide, style, b, frame, { ...base, ...(b.camera || {}) }) };
        if (frame && frame !== "wide" && b.hud) b = { ...b, hud: null };
      }
      const f = kind === "sheet" ? sheetFrame(g, style, A) : sceneFrame(g, style, A, b);
      return layered(f, layer === "all" ? null : layer || (style === "painted" ? "art" : null));
    },
  };
}
export { INK, lin };
