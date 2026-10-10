// "What it draws": the drawing and colour features of this release, each with the
// number that checks it. The numbers come from the evidence files committed with
// the release (docs/media/media.json reads them), so the film cannot quote a
// different build's results.
import { rect, circle, parsePath, polyline, contour } from "@raw-native/motion/path.mjs";
import { text } from "@raw-native/motion/text.mjs";
import { span, window, ease, lerp } from "@raw-native/motion/timeline.mjs";
import { sayTimeline } from "@raw-native/motion/narration.mjs";

const C = { ink: "#ece5d6", ink2: "#b8b0a0", dim: "#958e80", frame: "#2a2830", ok: "#63d4ce", hot: "#e29472", deep: "#1b2a4a" };
// sRGB hex to linear light, for the chapter drawn through a tone mapper.
const lin = (hex, k = 1) => {
  const n = (i) => { const v = parseInt(hex.slice(i, i + 2), 16) / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
  return [n(1) * k, n(3) * k, n(5) * k, 1];
};
const TONES = [["clip/srgb", "clip"], ["agx/srgb", "AgX"], ["aces2-sdr/srgb", "ACES 2.0"]];

export default {
  title: "What it draws",
  burnsCaptions: true,
  duration: 50,
  fps: 30,
  chapters: [],
  async load(ctx) {
    const facts = await ctx.json("../facts.json"), F = facts.facts, v = (k) => F[k].value;
    const timing = await ctx.json("../timing.json").catch(() => null);
    const cases = v("vector_cases"), pipes = v("colour_gpu");
    const edgeMean = Math.max(...Object.values(cases).map((c) => c.edge.mean));
    const gpuCodes = Math.max(...Object.values(pipes).map((p) => p.maxCode));
    const de = Number(v("aces2_ocio_de"));
    const lines = [
      `raw-native ${v("version")} draws more, and checks each new piece against a slow reference.`,
      "Strokes now take dashes, sharp or bevelled joins, and square ends.",
      `Over ${Object.keys(cases).length} test drawings, their edges sit within ${edgeMean.toFixed(3)} of the reference on average.`,
      "Shapes can be filled with gradients or images, and any shape can cut another.",
      "Light brighter than the screen can show goes through a tone mapper: clip, AgX, or ACES 2.0.",
      `The GPU matches its C++ reference within ${gpuCodes} step in 255.`,
      `And the C++ version of ACES 2.0 matches OpenColorIO within a colour difference of ${de.toFixed(4)}.`,
      "Every number here comes from the files in this release.",
    ];
    const tl = sayTimeline(lines, { timing });
    this.duration = tl.duration;
    this.captions = () => tl.vtt();
    this.chapters = [{ t: 0, title: "Strokes" }, { t: tl.at(3), title: "Paint and clips" }, { t: tl.at(4), title: "Colour" }, { t: tl.at(7), title: "The files" }];
    const atlas = await ctx.json("atlas.json");
    const T = (s, size, x, y, anchor = "center") => text(s, { atlas, size, x, y, anchor }).shape;
    // A 16 x 16 checker for the image fill, registered with the renderer when there is one.
    const img = new Uint8Array(16 * 16 * 4);
    for (let j = 0; j < 16; j++) for (let i = 0; i < 16; i++) img.set((i + j) % 2 ? [0x1b, 0x2a, 0x4a, 255] : [0xe8, 0xa0, 0x6a, 255], 4 * (j * 16 + i));
    if (ctx.motion && ctx.motion.image) ctx.motion.image("checker", { width: 16, height: 16, data: img });
    const A = {
      tl, lines, atlas, edgeMean, gpuCodes, de, n: Object.keys(cases).length,
      zig: parsePath("M300 760 L560 330 L820 760 L1080 330 L1340 760 L1600 330", { tolerance: 0.2 }),
      ring: circle(960, 560, 300, 160),
      star: parsePath("M960 250 L1050 470 L1290 480 L1100 630 L1170 860 L960 730 L750 860 L820 630 L630 480 L870 470 Z"),
      L: {
        title: T("What it draws", 84, 960, 500), sub: T(`raw-native ${v("version")}, at commit ${(facts.commit || "").slice(0, 7)}`, 32, 960, 570),
        miter: T("miter", 48, 960, 220), bevel: T("bevel", 48, 960, 220), round: T("round", 48, 960, 220),
        edge: T(`edges within ${edgeMean.toFixed(3)} of the reference, ${Object.keys(cases).length} drawings`, 40, 960, 950),
        tones: TONES.map(([, name]) => T(name, 64, 960, 230)),
        codes: T(`GPU vs C++: within ${gpuCodes} / 255`, 48, 960, 880),
        ocio: T(`C++ vs OpenColorIO 2.6: colour difference ${de.toFixed(4)}`, 44, 960, 950),
        files: ["evidence/m1-vector-swiftshader.json", "evidence/m1-colour-gpu-swiftshader.json", "evidence/m1-colour-ocio.json"].map((s, k) => T(s, 32, 960, 470 + 56 * k)),
      },
    };
    return A;
  },
  frame(t, ctx) {
    const A = ctx.assets, tl = A.tl, items = [];
    const put = (shape, fill, o, extra = {}) => { if (o > 0) items.push({ shape, fill, opacity: o, screen: true, ...extra }); };
    put(A.L.title, C.ink, window(t, 0.2, tl.at(1) - 0.3, 0.8, 0.6));
    put(A.L.sub, C.ink2, window(t, 0.5, tl.at(1) - 0.3, 0.8, 0.6));

    // Strokes: one zigzag cycles miter, bevel, round; a ring of dashes marches.
    const s1 = window(t, tl.at(1), tl.at(3), 0.6, 0.6);
    if (s1 > 0) {
      const k = Math.min(2, Math.floor(span(t, tl.at(1), tl.at(2) + 0.5) * 3));
      const join = ["miter", "bevel", "round"][k];
      const draw = span(t, tl.at(1), tl.at(1) + 1.6, ease.out);
      items.push({ shape: A.zig, stroke: C.ink, width: 64, join, cap: "square", miterLimit: 6, opacity: s1 * (1 - span(t, tl.at(2), tl.at(2) + 0.6)), screen: true,
        dash: draw < 1 ? [3000 * draw, 4000] : undefined });
      put(A.L[join], C.ink2, s1 * (1 - span(t, tl.at(2), tl.at(2) + 0.6)));
      const r = span(t, tl.at(2), tl.at(2) + 0.8);
      if (r > 0) {
        items.push({ shape: A.ring, stroke: C.ok, width: 36, cap: "butt", dash: [70, 34], dashOffset: -60 * (t - tl.at(2)), opacity: s1 * r, screen: true });
        items.push({ shape: A.ring, stroke: C.ink, width: 6, cap: "round", dash: [0, 26], dashOffset: 30 * (t - tl.at(2)), opacity: s1 * r, screen: true });
        put(A.L.edge, C.ink2, s1 * r);
      }
    }

    // Paint and clips: a gradient star, an image star, and a disc that cuts them.
    const s2 = window(t, tl.at(3), tl.at(4), 0.6, 0.6);
    if (s2 > 0) {
      const sweep = span(t, tl.at(3), tl.at(3) + 2.5, ease.inOut);
      const grad = { linear: [600 + 700 * sweep, 250, 1300 + 700 * sweep, 860], stops: [[0, C.deep], [0.5, C.ok], [1, C.hot]] };
      const clipR = 60 + 520 * span(t, tl.at(3) + 1.2, tl.at(3) + 3.2, ease.inOut);
      items.push({ shape: A.star, fill: grad, opacity: s2, screen: true });
      items.push({ shape: A.star, fill: { image: "checker", rect: [630, 250, 660, 610] }, stroke: C.ink, width: 6, join: "miter", opacity: s2, screen: true,
        clip: circle(960, 560, clipR, 120) });
      items.push({ shape: circle(960, 560, clipR, 120), stroke: C.ink2, width: 3, dash: [12, 10], cap: "butt", opacity: s2 * 0.8, screen: true });
    }

    // Colour: the same over-bright lights through three tone mappers. The scene is
    // linear here, so its colours are linear light.
    let colour;
    const s3 = window(t, tl.at(4), tl.at(7), 0.5, 0.5);
    if (s3 > 0) {
      const k = Math.min(2, Math.floor(span(t, tl.at(4) + 0.3, tl.at(5) + 2.0) * 3));
      colour = TONES[k][0];
      for (let i = 0; i < 7; i++) {
        const e = 2 ** (i - 2);                     // 0.25 to 16 times diffuse white
        items.push({ shape: circle(360 + i * 200, 470, 80, 90), fill: lin(C.hot, e), opacity: 1, screen: true });
      }
      for (let i = 0; i < 64; i++) {
        const e = 2 ** (-4 + (i / 63) * 8);
        items.push({ shape: rect(320 + i * 20, 640, 20, 90), fill: [e, e, e, 1], screen: true });
      }
      put(A.L.tones[k], lin(C.ink), s3);
      const nums = window(t, tl.at(5), tl.at(7), 0.5, 0.5);
      put(A.L.codes, lin(C.ok), nums * span(t, tl.at(5), tl.at(5) + 0.6));
      put(A.L.ocio, lin(C.ink2), nums * span(t, tl.at(6), tl.at(6) + 0.6));
    }

    // The files.
    const s4 = window(t, tl.at(7), tl.duration, 0.6, 1.0);
    A.L.files.forEach((s, k) => put(s, C.ink2, s4 * span(t, tl.at(7) + k * 0.3, tl.at(7) + 0.6 + k * 0.3)));

    for (const c of tl.cues) {
      const o = window(t, c.start, c.end, 0.3, 0.3);
      if (o > 0) put(text(c.text, { atlas: A.atlas, size: 34, x: 960, y: 1030, anchor: "center" }).shape, colour ? lin(C.ink) : C.ink, o);
    }
    return { background: colour ? [0.0018, 0.0018, 0.0024] : [0.024, 0.024, 0.031], camera: { x: 960, y: 540, zoom: 1 }, items,
      post: colour ? { bloom: 0.15, threshold: 1.5, vignette: 0.2, grain: 0.004, colour } : { bloom: 0.2, threshold: 0.9, vignette: 0.25, grain: 0.006 } };
  },
};

