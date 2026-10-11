// The tidepool's pixel art: one fixed palette of 20 colours, 16 x 16 ground tiles and the
// sprites that sit on them (the moon, sea stacks, boulders, the tidepool, kelp, shells and
// the film 1 props). Drawn by hand as character rows, except where the shape is geometry
// (the moon's disc, the stacks' outlines, the kelp's sway, the sky's dither steps), which is
// computed here from hand-chosen numbers. No noise: every pixel is placed or follows a rule
// written below. Original art for raw-native under its licence.
import { pixelTile, mirror } from "../../../web/world/author.mjs";

// The palette: void and night for the sky, sea teals, bone and sand, rock, kelp, the crab's
// rust, one amber and the four prism colours that only the flare and Tallow's flame use.
export const SHORE = {
  K: "#0b0a12", N: "#141a33", B: "#23305a", T: "#2f5866", S: "#3f8a8a", F: "#9cc4b2", W: "#efe6cf",
  G: "#b8a585", D: "#7d6b5a", R: "#3d3a42", r: "#5d5865", k: "#4b5a2e", C: "#b4492b", c: "#e07a4c",
  A: "#f2b450", P: "#c9877d", y: "#f0e04a", e: "#e8443a", b: "#6fd3e8", v: "#8a5cf0",
};
export const SHORE_PALETTE = Object.values(SHORE);
const KEY = { ".": null, ...SHORE };
const T = (rows) => pixelTile(rows, KEY);
const BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];

// A canvas of w x h characters, drawn into with small helpers, then made a tile.
function canvas(w, h, ch = ".") {
  const g = Array.from({ length: h }, () => Array(w).fill(ch));
  return {
    g, w, h,
    set(x, y, c) { if (x >= 0 && y >= 0 && x < w && y < h && c) g[y][x] = c; },
    span(y, x0, x1, c) { for (let x = Math.max(0, x0); x <= Math.min(w - 1, x1); x++) g[y][x] = c; },
    // Lay hand-drawn rows at (x, y); "." in the rows leaves what is under.
    stamp(rows, x, y) { rows.forEach((row, j) => [...row].forEach((c, i) => c !== "." && this.set(x + i, y + j, c))); },
    tile() { return T(g.map((r) => r.join(""))); },
  };
}

// A two-colour ordered dither step: fraction f of the pixels take colour b.
const step = (a, b, f, x, y) => ((BAYER4[y & 3][x & 3] + 0.5) / 16 < f ? b : a);
function skyTile(a, b, f0, f1, stars = []) {
  const c = canvas(16, 16);
  for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) c.set(x, y, step(a, b, f0 + (f1 - f0) * (y / 15), x, y));
  for (const [x, y, s] of stars) c.set(x, y, s);
  return c.tile();
}

// ---- Ground tiles (16 x 16) ---------------------------------------------------------
const sand = T([
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGDDGGGGGGGGGGG",
  "GGGGGDGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGDGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
]);
const sand3 = T([
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGDDDDGG",
  "GGGGGGGGDDGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
]);
const sandPlain = T(Array(16).fill("GGGGGGGGGGGGGGGG"));
const sand2 = mirror(sand);
const sandShell = T([
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGPPGGGG",
  "GGGGGGGGGPWWPGGG",
  "GGGGGGGGGDPPDGGG",
  "GGGGGGGGGGDDGGGG",
  "GGGDDGGGGGGGGGGG",
  "GGGGGDDGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
  "GWGGGGGGGGGDDDGG",
  "GGGGGGGGGGGGGGDG",
  "GGGGGGGGGGGGGGGG",
  "GGGGGGGGGGGGGGGG",
]);
const wet = T([
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDGGDDDDDDDD",
  "DDDDDDDDGGDDDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDDDDFDD",
  "DDDDDDDDDDDDDDDD",
  "DDGGGDDDDDDDDDDD",
  "DDDDDGGDDDDDDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDGGGDDD",
  "DDDDDDDDDDDDDGGD",
  "DDDDDDDDDDDDDDDD",
  "GDDDDDDDDDDDDDDD",
  "GGDDDDDDDDDDDDGG",
]);
// Wet sand fading into dry sand below.
const wetToSand = (() => { const c = canvas(16, 16); for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) c.set(x, y, step("D", "G", y / 13, x, y)); c.set(5, 3, "F"); return c.tile(); })();
// The surf line: sea above, a scalloped foam edge, lace on the wet sand.
const foam = T([
  "TTTTTTTTTTTTTTTT",
  "TTTSSSTTTTTTTTTT",
  "TTTTTTSSTTTTTSST",
  "SSTTTTTTTTTTTTTS",
  "FFSSTTTTTTSSFFFF",
  "WFFFSSSSSSFFWWFF",
  "WWWFFFFFFFFWWWWW",
  "FWWWWWWWWWWWWFFF",
  "DFFWWWWFFFWWFFDD",
  "DDDFFFFDDDFFDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDFDDDDDDDDDDFDD",
  "DDDFFDDDDDDDFDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDDDDDDD",
  "DDDDDDDDDDDDDDDD",
]);
const foam2 = mirror(foam);
// The rock shelf: a top with moonlit lips, the face below, and both ends.
const shelfTop = T([
  "................",
  "................",
  "...rrrrr.....rr.",
  "rrrFFrrrrrrrrrFr",
  "rFFrrrrrrrrrrrrr",
  "rrrrrrRrrrrrrrrr",
  "rrrrrRRRrrrrrRrr",
  "rrrrrrrrrrrrRRRr",
  "rRrrrrrrrrrrrrrr",
  "RRRrrrrrrRrrrrrr",
  "rrrrrrrrRRRrrrrr",
  "rrrrrrrrrrrrrrrr",
  "RRRRRRRRRRRRRRRR",
  "RRRRRRNRRRRRRRRR",
  "RRRRRNRRRRRRNRRR",
  "RRRRRNRRRRRNRRRR",
]);
const shelfTop2 = mirror(shelfTop);
const shelfFace = T([
  "RRRRRNRRRRRNRRRR",
  "RRRRRNRRRRRRNRRR",
  "RRRRNRRRRRRRNRRR",
  "RRRRNRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR",
  "RRRrRRRRRRRRRRRR",
  "RRRRRRRRRRRRRNNR",
  "NRRRRRRRRRRRNRRR",
  "RNRRRRRRRRRRRRRR",
  "RRNRRRRRRRRRRRRR",
  "RRRRRRRRRRrRRRRR",
  "RRRRRRRRRRRRRRRR",
  "RRRRRRNRRRRRRRRR",
  "RRRRRRRNNRRRRRRR",
  "RRRRRRRRRNRRRRRR",
  "RRRRRRRRRRRRRRRR",
]);
const shelfEndR = T([
  "................",
  "................",
  "rr..............",
  "rrr.............",
  "rrrr............",
  "rrrrr...........",
  "rrrrrr..........",
  "rrrrrrr.........",
  "rrrrrrrr........",
  "rrrrrrrrR.......",
  "rrrrrrrrRR......",
  "rrrrrrrRRRR.....",
  "RRRRRRRRRRRR....",
  "RRRRRRRRRRRRR...",
  "RRRRRNRRRRRRRR..",
  "RRRRRNRRRRRRRRR.",
]);
const shelfFaceEndR = T([
  "RRRRNRRRRRRRRRRR",
  "RRRRNRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRR",
  "RRRRRRRRRRRRRRRD",
  "RRRRRRRRRRRRRRDG",
  "RRRRRRRRRRRRRDGG",
  "RRRRRRRRRRRRDGGG",
  "RRRRRRRRRRRDDGGG",
  "RRRRRRRRRRRDGGGG",
  "RRRRRRRRRRDGGGGG",
  "RRRRRRRRRRDGGGGG",
  "RRRRRRRRRDGGGGGG",
  "RRRRRRRRRDGGGGGG",
  "RRRRRRRRDGGGGGGG",
]);

// The shelf's flat top, seen from above at the 2.5D angle: barnacles, a crack, a lit pit.
const rockFlat = T([
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrRrrrrrr",
  "rrrrrrrrRRrrrrrr",
  "rrrrrrrRRrrrrrrr",
  "rrrrrrrrRrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
]);
const rockBarnacle = T([
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrWrrrrrrrrrrrr",
  "rrWrWrrrrrrrrrrr",
  "rrrWrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrkkr",
  "rrrrrrrrrrrrkkrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrWrr",
  "rrrrrrrrrrrrWrWr",
  "rrrrrrrrrrrrrWrr",
]);
const rockPit = T([
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrRRRRrrrrrrr",
  "rrrrRRNNRRrrrrrr",
  "rrrrRNTTNRrrrrrr",
  "rrrrRNTFNRrrrrrr",
  "rrrrrRNNRrrrrrrr",
  "rrrrrrRRrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrRrrrrr",
  "rrrrrrrrrRRrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
]);
const rockShade = T([
  "rrrrrrrrrrrrrrrr",
  "rrrrrrRRRrrrrrrr",
  "rrrrRRRRRRRrrrrr",
  "rrrRRRRRRRRRRrrr",
  "rrRRRRRRRRRRRRrr",
  "rrrRRRRRRRRRRRrr",
  "rrrrRRRRRRRRRrrr",
  "rrrrrrRRRRRrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
  "rrrrrrrrrrrrrrrr",
]);
const rockPlain = T(Array(16).fill("rrrrrrrrrrrrrrrr"));
const rockFlat2 = mirror(rockFlat);
const rockEdgeR = T([
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrrRRDG",
  "rrrrrrrrrrrrRRDG",
  "rrrrrrrrrrrrRRDG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrRRDDGG",
  "rrrrrrrrrrRRDGGG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrrRRDG",
  "rrrrrrrrrrrrRRDG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrRRDGG",
  "rrrrrrrrrrrRRDGG",
]);

// ---- Sky and sea tiles ----------------------------------------------------------------
const sky = {
  void: skyTile("K", "K", 0, 0), voidStar: skyTile("K", "K", 0, 0, [[4, 5, "W"], [11, 12, "F"]]),
  voidNight: skyTile("K", "N", 0, 0.5), voidNightStar: skyTile("K", "N", 0, 0.5, [[9, 3, "F"]]),
  night: skyTile("N", "N", 0, 0), nightStar: skyTile("N", "N", 0, 0, [[6, 9, "W"]]),
  nightBlue: skyTile("N", "B", 0, 0.5), blue: skyTile("B", "B", 0, 0),
  blueSlate: skyTile("B", "T", 0, 0.5),
  // The horizon: slate warming to dusty rose in the last rows.
  horizon: skyTile("T", "P", -0.25, 0.3),
};
function seaTile(base, crest, glint = 0) {
  const c = canvas(16, 16, base);
  // Crest dashes at hand-chosen places: (x, y, length).
  for (const [x, y, n] of [[3, 1, 3], [6, 2, 2], [11, 4, 3], [14, 5, 2], [0, 5, 1], [4, 8, 4], [8, 9, 1], [12, 12, 3], [0, 13, 2], [15, 13, 1]])
    for (let i = 0; i < n; i++) c.set((x + i) % 16, y, crest);
  // The moon's road: broken horizontal glints, densest at the centre column.
  if (glint) for (const [x, y, n] of [[5, 0, 6], [6, 3, 4], [3, 6, 9], [6, 7, 3], [4, 10, 7], [7, 11, 2], [5, 14, 5]]) for (let i = 0; i < n * glint; i++) c.set(x + i, y, i % 3 === 1 ? "F" : "W");
  return c.tile();
}
const sea = { far: seaTile("B", "T"), mid: seaTile("T", "S"), farGlint: seaTile("B", "F", 0.6), midGlint: seaTile("T", "W", 1) };

export const GROUND_TILES = [
  ["sand", sand], ["sand2", sand2], ["sand3", sand3], ["sandPlain", sandPlain], ["sandShell", sandShell], ["wet", wet], ["wetToSand", wetToSand], ["foam", foam], ["foam2", foam2],
  ["shelfTop", shelfTop], ["shelfTop2", shelfTop2], ["rockFlat", rockFlat], ["rockFlat2", rockFlat2], ["rockBarnacle", rockBarnacle], ["rockPit", rockPit], ["rockPlain", rockPlain], ["rockShade", rockShade], ["rockEdgeR", rockEdgeR], ["shelfFace", shelfFace], ["shelfEndR", shelfEndR], ["shelfFaceEndR", shelfFaceEndR],
];
export const SKY_TILES = Object.entries(sky);
export const SEA_TILES = Object.entries(sea);

// ---- Sprites ---------------------------------------------------------------------------
// The moon: a bone disc shaded toward the lower left in an ordered dither, a ring of halo,
// and three craters. The aperture of the corpus, low over the sea.
export function moon(R = 20) {
  const S = 2 * R + 12, c = canvas(S, S), m = S / 2 - 0.5;
  for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
    const d = Math.hypot(x - m, y - m);
    if (d <= R) {
      const shade = Math.max(0, ((m - x) + (y - m)) / (2 * R) + (d / R) * 0.35 - 0.2);
      c.set(x, y, step("W", "F", Math.min(1, shade), x, y));
    } else if (d <= R + 2.2) c.set(x, y, step(".", "F", 0.25, x, y));
    else if (d <= R + 5) c.set(x, y, step(".", "T", 0.12, x, y));
  }
  for (const [x, y] of [[m + 6, m - 7], [m - 3, m + 4], [m + 9, m + 5]]) { c.set(Math.round(x), Math.round(y), "G"); c.set(Math.round(x) + 1, Math.round(y), "F"); }
  return c.tile();
}

// Sea stacks on the horizon: each one a run of rows (from the top) given as [left, right]
// insets, filled with night and lit on its right edge by the moon.
export function stacks() {
  const c = canvas(160, 44), base = 43;
  const draw = (x0, spans) => spans.forEach(([l, r], j) => {
    const y = base - spans.length + 1 + j;
    c.span(y, x0 + l, x0 + r, "N");
    c.set(x0 + r, y, j % 5 === 2 ? "T" : "B");
    if (j === 0) c.span(y, x0 + l, x0 + r, "B");
  });
  const col = (top, w, lean, n) => Array.from({ length: n }, (_, j) => { const t = j / (n - 1), half = w * (0.55 + 0.45 * Math.sqrt(t)); const cx = lean * (1 - t); return [Math.round(cx - half), Math.round(cx + half)]; });
  draw(26, col(0, 9, 3, 40));                // the tall stack, leaning seaward
  draw(52, col(0, 6, -2, 24));               // its smaller twin
  draw(8, col(0, 5, 0, 12));
  draw(120, col(0, 14, 0, 14).map(([l, r], j) => (j < 3 ? [l + 8, r - 8] : [l, r])));   // a low reef far right
  // The arch: a bite out of the tall stack's foot where the sea shows through.
  for (let y = 36; y < 44; y++) for (let x = 22; x < 30; x++) if (Math.hypot(x - 25.5, y - 44) < 6.5) c.set(x, y, ".");
  return c.tile();
}

// A boulder, by hand.
export const boulder = T([
  "...........rrrrr................",
  "........rrrFFrrrrr..............",
  "......rrFFrrrrrrrrrr............",
  ".....rFFrrrrrrrrrrrrrr..........",
  "....rFrrrrrrrrrRrrrrrrrr........",
  "...rrrrrrrrrrrRRRrrrrrrrrr......",
  "..rrrrrrrrrrrrrrrrrrrrrrrrr.....",
  "..rrrrRrrrrrrrrrrrrrrrrrrrrr....",
  ".rrrrRRRrrrrrrrrrrrrrRRrrrrrr...",
  ".rrrrrrrrrrrrrrrrrrrRRRRrrrrrr..",
  "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrr..",
  "RRrrrrrrrrrrrRrrrrrrrrrrrrrrrrr.",
  "RRRRrrrrrrrrRRRrrrrrrrrrrrrrrRR.",
  "RRRRRRRRrrrrrrrrrrrrrrrrRRRRRRRR",
  "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
  ".RRRRRRRNRRRRRRRRRRRNRRRRRRRRRR.",
  "..RRRRRRRNNRRRRRRRRNRRRRRRRRRR..",
  "...DDRRRRRRRRRRRRRRRRRRRRRRDDD..",
  ".....DDDDDDDDDDDDDDDDDDDDDDD....",
]);

// The tidepool set into the shelf: a rim, still water with the moon in it, a starfish and an
// anemone. 48 x 20.
export const pool = T([
  "......rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr........",
  "...rrrRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRrrr.....",
  "..rRRNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNRRrr...",
  ".rRNTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTNRRr..",
  "rRNTTTTTTTTTTSSTTTTTTTTTTTTTTTTTTWWTTTTTTTTNRr..",
  "rRNTTTTTTTTTTTTTTTTTTTTTTTTTTTTTWFFWTTTTTTTTNRr.",
  "rRNTTTTPTTTTTTTTTTTTTTTTSSSTTTTTTWWTTTTTTTTTNRr.",
  "rRNTTTPPPTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTSSTTTNRr.",
  "rRNTTPPPPPTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTNRr.",
  "rRNTTTPPPTTTTTTTTTTTTkTTTTTTTTTTTTTTTTTTTTTTNRr.",
  "rRNTTTTPTTTTTTTTTTTTkTkTTTTTTTTTTTTcTcTTTTTTNRr.",
  "rRNTTTTTTTTTTTSSTTTTTkTTTTTTTTTTTTTcccTTTTTTNRr.",
  "rRNTTTTTTTTTTTTTTTTTTkTTTTTTTTTTTTcCcCcTTTTTNRr.",
  ".rRNTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTCCCTTTTTNRr..",
  ".rRRNNTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTNNRRr..",
  "..rrRRNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNRRrr...",
  "...rrrRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRrr.....",
  ".....rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr.......",
  "................................................",
  "................................................",
]);

// Kelp in the foreground: strands that sway by a hand-set curve, with leaves every few rows.
export function kelp(w = 40, h = 72, strands = [[6, 0.0, 9], [16, 1.3, 7], [27, 2.4, 10], [34, 0.7, 6]]) {
  const c = canvas(w, h);
  for (const [x0, ph, amp] of strands) for (let y = 0; y < h; y++) {
    const t = 1 - y / h, x = Math.round(x0 + Math.sin(ph + t * 3.1) * amp * t);
    c.set(x, y, "K"); c.set(x + 1, y, y % 3 ? "K" : "k");
    if (y % 7 === 3 && y < h - 6) for (let i = 1; i < 5; i++) c.set(x + 1 + i, y - (i >> 1), i < 4 ? "k" : "K");
    if (y % 9 === 6 && y < h - 6) for (let i = 1; i < 4; i++) c.set(x - i, y - (i >> 1), "k");
  }
  // A dark rock the kelp grows from.
  for (let y = h - 10; y < h; y++) c.span(y, 0, Math.round(w * (0.5 + 0.5 * (y - h + 10) / 10)), "K");
  return c.tile();
}

// A claim shell, in three states: not yet knocked, holds (bone), hollow (dark).
const shellRows = (o, i, l) => [
  "....oooo....",
  "..ooiiiioo..",
  ".oiilllliio.",
  "oiiliiiiliio",
  "oiiliilliilo",
  "oiiiiiiiiiio",
  ".oooooooooo.",
  "..DDDDDDDD..",
].map((r) => r.replace(/o/g, o).replace(/i/g, i).replace(/l/g, l));
export const shellTiles = {
  unknocked: T(shellRows("R", "F", "W")), holds: T(shellRows("D", "W", "G")), hollow: T(shellRows("K", "N", "B")),
};
// A claim card lying on the sand, a paper boat and a cone, for the other guides' beats.
export const card = T([
  "WWWWWWWWWW..",
  "WDDDDDDWWWD.",
  "WWWWWWWWWWD.",
  "WDDDDWWWWWD.",
  "WWWWWWWWWWD.",
  ".DDDDDDDDDD.",
]);
export const boat = T([
  "......W.......",
  ".....WWF......",
  "....WWWFF.....",
  "...WWWWFFF....",
  "WWWWWWWWWWWWWW",
  ".GWWWWWWWWWWG.",
  "..GGGGGGGGGG..",
  "...DDDDDDDD...",
]);
export const cone = T([
  "....cc...",
  "....cC...",
  "...WWWW..",
  "...cccC..",
  "..ccccCC.",
  "..WWWWWW.",
  ".cccccCCC",
  "CCCCCCCCC",
  ".DDDDDDD.",
]);

// Things on the sand: a starfish, a run of pebbles and a length of driftwood.
export const starfish = T([
  "....P....",
  "....P....",
  "...PcP...",
  "PPPPcPPPP",
  ".PPcccPP.",
  "..PPcPP..",
  ".PP...PP.",
  "PP.....PP",
  "DD.....DD",
]);
export const pebbles = T([
  "..rr.........FF.",
  ".rFrr...R..rrrr.",
  ".rrrR..RrR.rrrRD",
  "..DD....D...DDD.",
]);
export const driftwood = T([
  "...........DDD..............",
  "..GGGGGGGGGGGDDGGGGGGGGGGG..",
  ".GDDDDGGGGGGGGGGGGGDDDGGGGG.",
  "GGGGGGGGDDDDDGGGGGGGGGGGGDGr",
  ".DDGGGGGGGGGGGGGDDDDGGGGGGDr",
  "..DDDDDDDDDDDDDDDDDDDDDDDDr.",
  "....RRRRRRRRRRRRRRRRRRRRR...",
]);
