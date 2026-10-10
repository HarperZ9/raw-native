// "Looks": one frame through the shader library's passes in turn: the Studio's tube,
// the physical tube, colour negative film, and old palettes with ordered dither. The
// closing number is from the pass gate's evidence committed with the release.
import { circle, parsePath } from "@raw-native/motion/path.mjs";
import { text } from "@raw-native/motion/text.mjs";
import { span, window, ease } from "@raw-native/motion/timeline.mjs";
import { sayTimeline } from "@raw-native/motion/narration.mjs";

const lin = (hex) => [1, 3, 5].map((i) => { const v = parseInt(hex.slice(i, i + 2), 16) / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }).concat(1);
const C = { ink: "#ece5d6", ink2: "#b8b0a0", ok: "#63d4ce", hot: "#e29472", deep: "#1b2a4a" };
// One look per sentence 1 to 5; the last sentence shows the frame plain again.
const LOOKS = [
  null,
  { name: "the Studio's tube", passes: [{ pass: "crt-classic", cell: 4, scanlines: true, scanStrength: 0.45, beam: 0.6, mask: "grille", maskStrength: 0.45, bloom: 0.2, halation: 0.25, curvature: 0.12, aberration: 0.15, vignette: 0.3 }] },
  { name: "a physical tube", passes: [{ pass: "crt", preset: "pvm-20", source: [320, 180] }] },
  { name: "colour negative film, printed", passes: [{ pass: "film", preset: "500t-print" }] },
  { name: "PICO-8, ordered dither", passes: [{ pass: "dither", palette: "pico8", mode: "bayer4" }] },
  { name: "Game Boy, ordered dither", passes: [{ pass: "dither", palette: "gameboy", mode: "bayer8" }] },
];

export default {
  title: "Looks",
  burnsCaptions: false,   // the looks would degrade burned words; the page and player show the captions
  duration: 40,
  fps: 30,
  chapters: [],
  async load(ctx) {
    const facts = await ctx.json("../facts.json"), F = facts.facts, v = (k) => F[k].value;
    const timing = await ctx.json("../timing.json").catch(() => null);
    const gate = v("post_gate");
    const worst = Math.max(...Object.values(gate).flatMap((p) => (p.runs || []).map((r) => r.maxCode)));
    const lines = [
      `raw-native ${v("version")} can make a frame look as if it came through old hardware or old film.`,
      "This is the Studio's tube: scanlines, a mask, bloom and a curved face.",
      "This is a physical model of a tube, from the signal to the glass.",
      "This is colour negative film, printed, with its grain and halation.",
      "And these are the palettes of old machines, with ordered dither.",
      `Each one runs on the GPU and matches its slower reference within ${worst} step in 255.`,
    ];
    const tl = sayTimeline(lines, { timing });
    this.duration = tl.duration;
    this.captions = () => tl.vtt();
    this.chapters = [{ t: 0, title: "The frame" }, { t: tl.at(1), title: "Tubes" }, { t: tl.at(3), title: "Film" }, { t: tl.at(4), title: "Palettes" }, { t: tl.at(5), title: "The check" }];
    const atlas = await ctx.json("atlas.json");
    const T = (s, size, x, y, anchor = "center") => text(s, { atlas, size, x, y, anchor }).shape;
    return {
      tl, atlas, worst,
      star: parsePath("M960 240 L1040 450 L1260 455 L1085 590 L1150 800 L960 680 L770 800 L835 590 L660 455 L880 450 Z"),
      title: T("raw-native", 110, 960, 175),
      names: LOOKS.map((l) => (l ? T(l.name, 44, 960, 955) : null)),
    };
  },
  frame(t, ctx) {
    const A = ctx.assets, tl = A.tl, items = [];
    const put = (shape, fill, o = 1, extra = {}) => { if (o > 0) items.push({ shape, fill, opacity: o, screen: true, ...extra }); };
    // Which look: one per sentence.
    const k = Math.min(LOOKS.length - 1, Math.max(0, tl.cues.findIndex((c) => t < c.end)));
    let look = LOOKS[k === 4 && t > (tl.at(4) + tl.at(5)) / 2 ? 5 : k];
    if (tl.cues.length && t >= tl.at(5)) look = null;
    // Film takes scene light, so its chapter gives linear colours and brighter lights.
    const film = look && look.passes[0].pass === "film";
    const L = (hex) => (film ? lin(hex) : hex), boost = film ? 6 : 1;
    // The frame every look is applied to: a title, a star and a row of lights.
    put(A.title, L(C.ink), window(t, 0.2, tl.duration, 0.8, 0.5));
    const spin = 0.15 * Math.sin(t * 0.6);
    put(A.star, { linear: [700, 250 + 200 * spin, 1220, 800], stops: [[0, L(C.deep)], [0.5, L(C.ok)], [1, L(C.hot)]] }, 1, { stroke: L(C.ink), width: 6, join: "miter" });
    for (let i = 0; i < 7; i++) put(circle(360 + i * 200, 870 - 14 * Math.sin(t * 1.4 + i), 34, 60), [0.9 * boost, (0.55 + 0.05 * i) * boost, 0.4 * boost, 1], 0.4 + 0.1 * i);
    const name = look ? A.names[LOOKS.indexOf(look)] : null;
    if (name) put(name, C.ink2, 1);
    return { background: [0.024, 0.024, 0.031], camera: { x: 960, y: 540, zoom: 1 }, items,
      post: { bloom: 0.2, threshold: 0.9, vignette: 0.2, grain: 0.004, passes: look ? look.passes : [] } };
  },
};
