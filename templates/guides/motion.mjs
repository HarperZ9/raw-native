// Motion for the guides: framings (wide, medium, close) and three clips per guide, an idle
// loop, a talk loop and one signature action. Each clip is a function of time that returns
// the guide's pose options and any extra things in the scene; loops repeat exactly (every
// cycle is a whole number of periods of the loop length). Pure: runs in Node.
import { S as isoS, depth as isoDepth, groundShadow } from "./worlds/iso_kit.mjs";
import { coneParts } from "./pylon.mjs";
import { P, INK } from "./rig.mjs";
import { sprite } from "./worlds/tidepool.mjs";

export const CLIPS = { idle: 2.4, talk: 2.4, action: 3.2 };
const TAU = Math.PI * 2;
const wave = (t, T, k, ph = 0) => Math.sin(TAU * (k * t / T) + ph);
const smooth = (a, b, t) => { const x = Math.min(1, Math.max(0, (t - a) / (b - a))); return x * x * (3 - 2 * x); };
// A blink: the lid closes and opens over 0.16 s, starting at t0.
const blink = (t, t0, base) => { const u = (t - t0) / 0.16; return u < 0 || u > 1 ? base : base + (1 - base) * (1 - Math.abs(2 * u - 1)); };
// Syllables: a fixed pattern of mouth openings, eight to the loop.
const SYL = [0.9, 0.4, 1, 0.2, 0.7, 0.95, 0.3, 0.6];
const talkOpen = (t, T) => { const n = SYL.length, u = (t / T) * n, i = Math.floor(u) % n; return SYL[i] * Math.sin(Math.PI * (u - Math.floor(u))); };

// Guide heights in rig units (feet to the top), for framing.
const HEIGHT = { tallow: 830, pylon: 650, rerun: 420 };

// The camera for a framing of a beat in its style. Wide keeps the beat's own camera.
export function framing(guide, style, beat, name, base) {
  if (!name || name === "wide") return {};
  let x = beat.x, y = beat.y;
  if (beat.cell) [x, y] = isoS(...beat.cell);
  const h = HEIGHT[guide] * beat.s;
  if (style === "pixel") {
    // Pixel mode keeps one art pixel per frame pixel: a medium frame renders at 240 x 135
    // with zoom 2, a close one at 160 x 90 with zoom 3 (PROGRESS.md, step 12: the guide fills
    // about half and about three quarters of the frame height).
    const [zoom, w, hh] = name === "close" ? [3, 160, 90] : [2, 240, 135];
    const grid = 8;
    // The foreground kelp sits at 1.5x parallax and would cross the guide in a close frame: dropped.
    return { noFront: true, camera: { x: Math.round((x + 20) / grid) * grid, y: Math.round((y - h * (name === "close" ? 0.55 : 0.45)) / grid) * grid, zoom }, pixel: { width: w, height: hh, mode: "integer" } };
  }
  // Guide height as a share of the frame, and the point framed (a share of the way up the body).
  // Medium shows the guide at 0.85 of the frame (knees up for Tallow), close at 1.5 (head and
  // shoulders); PROGRESS.md, step 12.
  const k = name === "close" ? [1.5, 0.74] : [0.85, 0.52];
  const zoom = (1080 * k[0]) / h;
  return { camera: { ...base, x: x + (name === "close" ? 0 : 60 / zoom), y: y - h * k[1], zoom } };
}

// Tallow: breath and a flickering flame; talk with the mouth and the free hand; the flame gutters.
function tallow(kind, t, o) {
  const T = CLIPS[kind] || 2.4;
  if (kind === "idle") return { ...o, flame: 1 + 0.05 * wave(t, T, 7) + 0.03 * wave(t, T, 11, 1), lean: (o.lean || 0) + 4 * wave(t, T, 3), lid: blink(t, 1.5, o.lid ?? 0.5), dy: 3 * wave(t, T, 1) };
  if (kind === "talk") return { ...o, mouth: 0.15 + 0.85 * talkOpen(t, T), flame: 1 + 0.04 * wave(t, T, 9), lean: 3 * wave(t, T, 4),
    armR: 18 + 14 * (0.5 + 0.5 * wave(t, T, 1)), elbR: 70 + 30 * (0.5 + 0.5 * wave(t, T, 2, 0.6)), prop: null, lid: blink(t, 0.9, 0.5), dy: 1.5 * wave(t, T, 2) };
  // The flame gutters: it shrinks, thrashes, nearly goes, and comes back. His eye narrows.
  const low = smooth(0.5, 1.1, t) * (1 - smooth(1.9, 2.7, t));
  return { ...o, flame: 1 - 0.68 * low + 0.12 * low * wave(t, 0.4, 1), lean: 26 * low * wave(t, 0.5, 1) + 4 * wave(t, T, 2), lid: (o.lid ?? 0.5) + 0.25 * low, dy: 0 };
}

// Pylon: a slow sway and a blink; talk with the bark and nods; she places a cone on a claim.
function pylon(kind, t, o) {
  const T = CLIPS[kind] || 2.4;
  if (kind === "idle") return { ...o, tilt: (o.tilt || 0) + 3 * wave(t, T, 1), lid: blink(t, 1.7, o.lid ?? 0.5), dy: 3 * wave(t, T, 2), flipR: (o.flipR || 0) * (0.85 + 0.15 * wave(t, T, 1, 1)) };
  if (kind === "talk") return { ...o, bark: 0.1 + 0.4 * talkOpen(t, T), tilt: (o.tilt || 0) + 4 * wave(t, T, 2), lid: blink(t, 1.2, 0.5), dy: 2 * wave(t, T, 4) };
  // Place a cone: the right flipper rises with a cone, swings down to the claim, lets go, returns.
  const up = smooth(0.1, 0.8, t), down = smooth(0.8, 1.5, t), back = smooth(1.9, 2.6, t);
  const flipR = 20 + 75 * up - 40 * down * (1 - back) - 55 * back;
  return { ...o, card: false, board: false, flipR, tilt: (o.tilt || 0) - 9 * down * (1 - back), lid: blink(t, 2.75, 0.5), look: [6 * (1 - back), 6 * down * (1 - back)], coneT: t };
}

// Rerun: stalks bob and the flare twinkles; talk with the small claw; knock, knock: hollow.
function rerun(kind, t, o) {
  const T = CLIPS[kind] || 2.4;
  if (kind === "idle") return { ...o, stalk: 1 + 0.08 * wave(t, T, 2), flare: 0.85 + 0.15 * wave(t, T, 3), clawR: (o.clawR || 0) * 0.3 + 6 * wave(t, T, 1), clawL: 4 * wave(t, T, 1, 2), look: [3 * wave(t, T, 1), 0], dy: 2 * wave(t, T, 2) };
  if (kind === "talk") return { ...o, clawL: 18 + 22 * talkOpen(t, T), stalk: 1.05 + 0.1 * talkOpen(t, T), clawR: 8, look: [0, -2], dy: 1.5 * wave(t, T, 4) };
  // Two knocks, a pause, the shell rings hollow, and Rerun backs off.
  const strike = (t0) => smooth(t0 - 0.28, t0 - 0.08, t) * (1 - smooth(t0, t0 + 0.06, t));
  const raise = Math.max(strike(0.75), strike(1.3)), after = smooth(1.8, 2.8, t);
  return { ...o, clawR: -4 + 100 * raise, stalk: 1.2 - 0.35 * after, look: [5 * (1 - after), -2], dx: -40 * after, knockT: t };
}
const BY = { tallow, pylon, rerun };

// The beat at time t of a clip: pose options, a small body offset, and the action's props.
export function clipBeat(guide, kind, beat, t) {
  const o = BY[guide](kind, t, beat.o || {});
  const b = { ...beat, o, offset: [(o.dx || 0) * beat.s, (o.dy || 0) * beat.s] };
  if (guide === "pylon" && kind === "action") b.things = withCone(beat, o);
  if (guide === "rerun" && kind === "action") b.front = knocking(beat, o);
  return b;
}

// Pylon's cone: carried at the right flipper's tip, then left standing on the claim it marks.
function withCone(beat, o) {
  const base = beat.things ? beat.things() : [];
  return () => {
    const [x, y] = isoS(...beat.cell), s = beat.s, a = (-o.flipR * Math.PI) / 180;
    const tx = 30, ty = 150, px = 138 + Math.cos(a) * tx - Math.sin(a) * ty, py = -236 + Math.sin(a) * tx + Math.cos(a) * ty;
    const target = [beat.cell[0] + 1.2, beat.cell[1] - 0.3], [gx, gy] = isoS(...target);
    const placed = o.coneT >= 1.5, cx = placed ? gx : x + px * s, cy = placed ? gy : y + py * s + 30 * s;
    const cone = coneParts(cx, cy, { scale: 0.5 * s * 2 }).map((p) => ({ shape: P(p.d), fill: p.fill, stroke: INK, width: 2 }));
    const boat = [{ shape: P(`M ${gx - 34} ${gy - 8} L ${gx + 34} ${gy - 8} L ${gx + 20} ${gy + 6} L ${gx - 20} ${gy + 6} Z`), fill: "#ddd2b6", stroke: INK, width: 2.5 },
      { shape: P(`M ${gx - 16} ${gy - 8} L ${gx + 2} ${gy - 46} L ${gx + 16} ${gy - 8} Z`), fill: "#efe6d0", stroke: INK, width: 2.5 }];
    return [...base.filter((th) => th.d < isoDepth(...target) - 0.5 || th.d > isoDepth(...target) + 0.5),
      { d: isoDepth(...target), items: [...groundShadow(...target, 0.3, 0.8), ...boat, ...(placed ? cone : [])] },
      ...(placed ? [] : [{ d: isoDepth(...beat.cell) + 1, items: cone }])];
  };
}

// Rerun's knock: one shell beside the big claw; ring marks on each hit; it goes dark (hollow).
function knocking(beat, o) {
  return (style, A) => {
    const items = [], t = o.knockT, lx = 182, ly = 220;
    items.push(sprite(A.pixel, t > 1.7 ? "hollow" : "unknocked", lx, ly));
    const hit = (t > 0.75 && t < 0.95) || (t > 1.3 && t < 1.5) || (t > 1.9 && t < 2.4);
    if (hit) for (const [dx, dy, w] of [[-3, -3, 3], [12, -4, 3], [-4, 2, 2], [13, 3, 2]]) items.push({ shape: P(`M ${(lx + dx) * 4} ${(ly + dy) * 4} L ${(lx + dx + w) * 4} ${(ly + dy) * 4}`), fill: null, stroke: "#efe6cf", width: 4 });
    return items;
  };
}
