// Tallow, the night clerk: a tall, stooped clerk whose head is a lit candle, one eye in the
// wax under the wick. The wax is the budget of hours; the flame is what has been checked.
// Original character, drawn here in code (raw-native licence). Front view, feet at the
// origin, about 800 units from the soles to the flame tip.
import { ellipse, lin, rad, limb, hatch, INK } from "./rig.mjs";

export const TALLOW = {
  coat: "#262531", coatLit: "#4a4252", trouser: "#17161c", shoe: "#0d0c0e", wax: "#efe3c9", waxMid: "#d6c29b",
  waxShade: "#9c845c", hand: "#d8c6a5", ledger: "#6e2a22", page: "#ebdfc2", brass: "#b8892e", iris: "#6b4420",
};
const C = TALLOW;
// The flame's tip, for lights and glows the scene adds.
export const FLAME = [6, -738];

// opts: armL, armR (degrees at the shoulder, + swings the hand outward), prop ("ledger" | "card" |
// "jar" | "loupe" | null), propL ("ledger" | "jar" | null), lid (0 open .. 1 shut), look ([dx, dy]
// of the iris), flame (scale), lean (flame lean in degrees), drip (0..1 of a drip pinched off)
export function tallow(o = {}) {
  const { armL = 10, armR = 6, prop = null, propL = "ledger", lid = 0.42, look = [0, 0], flame = 1, lean = 0, mouth = 0 } = o;
  const parts = [];
  const add = (p) => parts.push(p);
  // Shoes and legs.
  for (const sgn of [-1, 1]) {
    const x = 26 * sgn;
    add({ d: `M ${x - 10 * sgn} -22 C ${x + 4 * sgn} -26 ${x + 44 * sgn} -18 ${x + 52 * sgn} -4 C ${x + 40 * sgn} 2 ${x + 4 * sgn} 2 ${x - 14 * sgn} 0 Z`,
      pivot: [x, -10, 10], dz: -60, fill: C.shoe, stroke: INK, width: 3, side: 0.8 });
    add({ d: limb(x, -330, x + 2 * sgn, -18, 40, 26, 4 * sgn), pivot: [x, -170, 0], dz: -60, fill: lin(x - 20, 0, x + 20, 0, [0, "#232129"], [1, C.trouser]), stroke: INK, width: 3, side: 0.9 });
  }
  // The coat: long, flared, stooped.
  add({ d: "M -80 -505 C -104 -470 -114 -380 -120 -300 C -126 -220 -140 -160 -156 -104 C -96 -90 -36 -98 0 -108 C 36 -98 96 -90 156 -104 C 140 -160 126 -220 120 -300 C 114 -380 104 -470 80 -505 C 40 -520 -40 -520 -80 -505 Z",
    pivot: [0, -300, 0], side: 0.62, fill: lin(-150, -500, 150, -110, [0, C.coatLit], [0.45, C.coat], [1, "#16151c"]), stroke: INK, width: 4, name: "coat" });
  // Folds and the front edge, drawn as ink.
  add({ d: "M -4 -470 C -2 -380 -8 -250 -2 -110", pivot: [0, -300, 30], fill: null, stroke: INK, width: 3, face: 0, flat: 0.8 });
  add({ d: "M -70 -330 C -84 -260 -96 -190 -110 -120 M 64 -360 C 78 -280 92 -200 112 -118", pivot: [0, -300, 20], fill: null, stroke: "#0f0e14", width: 2.5, face: 0, flat: 0.6 });
  for (const yb of [-430, -372, -314]) add({ d: ellipse(10, yb, 6, 6), pivot: [10, yb, 34], fill: C.brass, stroke: INK, width: 2, face: 0, flat: 1 });
  // Lapels and the high collar.
  add({ d: "M -6 -470 L -44 -505 L -70 -500 C -56 -470 -36 -440 -10 -400 Z", pivot: [-30, -470, 30], fill: "#1c1b23", stroke: INK, width: 3, face: 0, flat: 0.7 });
  add({ d: "M 6 -470 L 44 -505 L 70 -500 C 56 -470 36 -440 10 -400 Z", pivot: [30, -470, 30], fill: "#1c1b23", stroke: INK, width: 3, face: 0, flat: 0.7 });
  add({ d: hatch(-160, -520, 160, -90, { gap: 8, angle: 62, wobble: 5, seed: "tallow-coat" }), pivot: [0, -300, 1], side: 0.62, fill: null, stroke: [0.02, 0.02, 0.03, 0.55], width: 1.4, clip: "coat" });
  add({ d: hatch(-160, -520, 160, -90, { gap: 13, angle: -30, wobble: 7, seed: "tallow-coat-2" }), pivot: [0, -300, 1], side: 0.62, fill: null, stroke: [0.02, 0.02, 0.03, 0.3], width: 1.2, clip: "coat" });
  // Wax spattered on the shoulders: hours already spent.
  add({ d: "M -70 -500 C -66 -512 -50 -512 -48 -500 C -50 -486 -56 -470 -60 -478 C -62 -488 -72 -490 -70 -500 Z", pivot: [-60, -495, 12], fill: C.waxMid, stroke: INK, width: 2 });
  add({ d: "M 52 -498 C 56 -508 72 -506 72 -496 C 70 -488 66 -462 60 -470 C 58 -480 50 -488 52 -498 Z", pivot: [62, -490, 12], fill: C.waxMid, stroke: INK, width: 2 });
  add({ d: "M -78 -500 C -86 -540 -64 -566 -46 -548 L -34 -500 Z", pivot: [-56, -520, 4], dz: -30, fill: C.coat, stroke: INK, width: 3 });
  add({ d: "M 78 -500 C 86 -540 64 -566 46 -548 L 34 -500 Z", pivot: [56, -520, 4], dz: -30, fill: C.coat, stroke: INK, width: 3 });
  // The candle head, with its drips, the pool and the wick.
  add({ d: "M -44 -492 L -46 -636 C -46 -650 -30 -658 -14 -654 C -4 -662 8 -661 16 -655 C 30 -660 46 -650 46 -638 L 44 -492 C 20 -484 -20 -484 -44 -492 Z",
    pivot: [0, -570, 0], fill: lin(-46, 0, 46, 0, [0, C.waxShade], [0.28, C.wax], [0.62, C.wax], [1, C.waxMid]), stroke: INK, width: 3.5, name: "head" });
  add({ d: hatch(-50, -660, 50, -490, { gap: 7, angle: 100, wobble: 2, seed: "tallow-wax" }), pivot: [0, -570, 1], fill: null, stroke: [0.42, 0.3, 0.16, 0.35], width: 1.3, clip: "head" });
  add({ d: "M 46 -632 C 56 -600 54 -560 50 -536 C 47 -524 38 -526 38 -540 L 40 -628 Z", pivot: [46, -580, 4], fill: C.waxMid, stroke: INK, width: 2.5 });
  add({ d: "M -22 -652 C -18 -626 -28 -606 -24 -596 C -19 -588 -12 -594 -12 -606 L -8 -652 Z", pivot: [-16, -620, 46], fill: C.wax, stroke: INK, width: 2, face: 0, flat: 0.9 });
  add({ d: "M -46 -620 C -56 -590 -52 -566 -46 -560 Z", pivot: [-46, -590, 4], fill: C.waxMid, stroke: INK, width: 2 });
  add({ d: "M -38 -650 C -26 -664 30 -664 40 -650 C 28 -642 -26 -642 -38 -650 Z", pivot: [0, -652, 10], fill: "#fff5dc", stroke: null, opacity: 0.95 });
  add({ d: "M 2 -655 C 0 -668 8 -676 4 -688", pivot: [2, -670, 12], fill: null, stroke: INK, width: 4 });
  // The eye: sclera, iris, pupil, a highlight, a heavy lid and a bag beneath.
  const [lx, ly] = look, ey = -572, lidY = -592 + 34 * lid;
  add({ d: "M -28 -572 C -14 -590 14 -590 28 -572 C 14 -556 -14 -556 -28 -572 Z", pivot: [0, ey, 46], fill: "#f6eedd", stroke: INK, width: 2.5, face: 0, flat: 1, name: "eye" });
  add({ d: ellipse(2 + lx, ey + ly, 11, 11), pivot: [0, ey, 47], fill: rad(2 + lx, ey + ly, 11, [0, "#e3a24a"], [0.55, C.iris], [1, "#2a1a0e"]), clip: "eye", face: 0, flat: 1 });
  add({ d: ellipse(2 + lx, ey + ly, 5, 5), pivot: [0, ey, 48], fill: "#090808", clip: "eye", face: 0, flat: 1 });
  add({ d: ellipse(6 + lx, ey + ly - 4, 2.5, 2.5), pivot: [0, ey, 49], fill: "#ffffff", clip: "eye", face: 0, flat: 1 });
  add({ d: `M -30 -571 C -14 -594 14 -594 30 -571 C 14 ${lidY} -14 ${lidY} -30 -571 Z`, pivot: [0, ey, 50], fill: lin(0, -594, 0, lidY, [0, C.wax], [1, C.waxMid]), stroke: INK, width: 2.5, face: 0, flat: 1 });
  add({ d: "M -20 -555 C -8 -549 8 -549 20 -555", pivot: [0, -553, 46], fill: null, stroke: C.waxShade, width: 2.5, face: 0, flat: 1 });
  if (mouth) add({ d: `M -12 -528 C -4 ${-528 + 10 * mouth} 6 ${-528 + 10 * mouth} 14 -530`, pivot: [0, -528, 45], fill: null, stroke: C.waxShade, width: 3, face: 0, flat: 1 });
  // Arms: sleeves that swing at the shoulder, each with a hand and an optional prop.
  const arm = (sgn, ang, held) => {
    const sx = 84 * sgn, sy = -488, rot = -ang * sgn;
    const sleeve = { d: `M ${sx - 6 * sgn} ${sy - 12} C ${sx + 30 * sgn} ${sy} ${sx + 40 * sgn} ${sy + 70} ${sx + 40 * sgn} ${sy + 140} L ${sx + 44 * sgn} ${sy + 226} L ${sx + 10 * sgn} ${sy + 230} L ${sx + 4 * sgn} ${sy + 140} C ${sx} ${sy + 80} ${sx - 10 * sgn} ${sy + 40} ${sx - 26 * sgn} ${sy + 20} Z`,
      pivot: [sx, sy, 0], rot, fill: lin(sx - 30, sy, sx + 40, sy + 220, [0, "#5a5163"], [1, "#2e2c38"]), stroke: INK, width: 3.5, dz: 30 };
    const hx = sx + 28 * sgn, hy = sy + 236;
    const hand = { d: `M ${hx - 16} ${hy - 6} C ${hx - 22} ${hy + 20} ${hx - 14} ${hy + 50} ${hx - 4} ${hy + 58} C ${hx + 4} ${hy + 50} ${hx + 2} ${hy + 30} ${hx + 6} ${hy + 46} C ${hx + 12} ${hy + 56} ${hx + 20} ${hy + 40} ${hx + 16} ${hy - 6} Z`,
      pivot: [sx, sy, 0], rot, fill: C.hand, stroke: INK, width: 2.5, dz: 29 };
    const out = [hand, sleeve];
    const P = (d, fill, extra = {}) => out.push({ d, pivot: [sx, sy, 0], rot, fill, stroke: INK, width: 2.5, dz: 31, ...extra });
    if (held === "ledger") {
      P(`M ${hx - 40} ${hy - 30} L ${hx + 34} ${hy - 44} L ${hx + 46} ${hy + 50} L ${hx - 28} ${hy + 64} Z`, C.ledger);
      P(`M ${hx + 34} ${hy - 44} L ${hx + 40} ${hy - 40} L ${hx + 52} ${hy + 52} L ${hx + 46} ${hy + 50} Z`, C.page);
      P(`M ${hx - 22} ${hy + 4} L ${hx + 22} ${hy - 4}`, null, { stroke: C.brass, width: 3 });
    } else if (held === "card" || held === "blank") {
      P(`M ${hx - 52} ${hy - 70} L ${hx + 40} ${hy - 82} L ${hx + 48} ${hy - 22} L ${hx - 44} ${hy - 10} Z`, C.page, { dz: 40 });
      if (held === "card") for (let i = 0; i < 4; i++) P(`M ${hx - 36} ${hy - 58 + i * 11 - i * 0.5} L ${hx + (i === 3 ? 0 : 30)} ${hy - 68 + i * 11}`, null, { stroke: "#5b4e3c", width: 2.5, dz: 41 });
    } else if (held === "jar") {
      P(`M ${hx - 30} ${hy + 4} L ${hx + 30} ${hy + 4} L ${hx + 34} ${hy + 86} C ${hx + 20} ${hy + 96} ${hx - 20} ${hy + 96} ${hx - 34} ${hy + 86} Z`, [0.75, 0.82, 0.8, 0.35], { dz: 40 });
      P(`M ${hx - 28} ${hy + 60} C ${hx - 10} ${hy + 52} ${hx + 10} ${hy + 66} ${hx + 30} ${hy + 56} L ${hx + 32} ${hy + 86} C ${hx + 20} ${hy + 94} ${hx - 20} ${hy + 94} ${hx - 32} ${hy + 86} Z`, C.waxMid, { dz: 39, stroke: null });
      P(`M ${hx - 24} ${hy + 22} L ${hx + 24} ${hy + 22} L ${hx + 24} ${hy + 40} L ${hx - 24} ${hy + 40} Z`, C.page, { dz: 41, width: 1.5 });
      P(`M ${hx - 34} ${hy - 6} L ${hx + 34} ${hy - 6} L ${hx + 32} ${hy + 6} L ${hx - 32} ${hy + 6} Z`, C.brass, { dz: 41 });
    } else if (held === "loupe") {
      P(ellipse(hx + 4, hy - 40, 30, 30), [0.85, 0.9, 0.95, 0.22], { stroke: C.brass, width: 7, dz: 40 });
      P(`M ${hx - 2} ${hy - 10} L ${hx + 2} ${hy + 30}`, null, { stroke: "#3b2a1a", width: 9, dz: 39 });
    }
    return out;
  };
  arm(-1, armL, propL).forEach(add);
  arm(1, armR, prop).forEach(add);
  // The flame, last: an outer tongue and a hot core, leaning by `lean`.
  const fx = FLAME[0], h = 92 * flame;
  add({ d: `M 4 -686 C ${-20 * flame} ${-700 - h * 0.15} ${-22 * flame} ${-700 - h * 0.6} ${fx} ${-686 - h} C ${26 * flame} ${-700 - h * 0.55} ${28 * flame} ${-700 - h * 0.15} 4 -686 Z`,
    pivot: [4, -686, 12], rot: lean, fill: rad(4, -700 - h * 0.25, h * 0.75, [0, "#fff8e8"], [0.35, "#ffd27a"], [0.75, "#ff8a2a"], [1, [1, 0.35, 0.05, 0]]), blend: "add", dz: 200 });
  add({ d: `M 4 -688 C -6 -696 -8 ${-700 - h * 0.3} 4 ${-690 - h * 0.55} C 14 ${-700 - h * 0.3} 12 -696 4 -688 Z`,
    pivot: [4, -686, 12], rot: lean, fill: rad(4, -700, h * 0.4, [0, "#ffffff"], [0.6, "#fff0c8"], [1, [0.6, 0.75, 1, 0.6]]), blend: "add", dz: 201 });
  return parts;
}
