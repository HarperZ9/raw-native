// Landmark sets from anthropometric proportions. Numbers are metres for an adult male and are the single place to correct.
// Sources (values quoted from memory, confidence per row; check against the sources before relying on a digit):
//  [ANSUR] Gordon, C.C. et al., 2012 Anthropometric Survey of US Army Personnel (ANSUR II), Natick TR-15/007: head length,
//          head breadth, bizygomatic, bigonial, biocular, nose and mouth breadths, menton-vertex height. Moderate confidence on each
//          mean, low on the third digit.
//  [FARKAS] Farkas, L.G. (ed.), Anthropometry of the Head and Face, 2nd ed., Raven Press, 1994: landmark definitions (glabella,
//          nasion, pronasale, subnasale, stomion, pogonion, menton, gonion, zygion, exocanthion, alare, cheilion), the facial thirds,
//          nose protrusion. Moderate confidence.
//  [LOOMIS] Loomis, A., Drawing the Head and Hands, 1956: the eye line at the middle of the head and the three equal facial
//          thirds. High confidence on the rule, which is a teaching rule and not a measurement.
// Age: Farkas reports the nose lengthening and the lips thinning with age. The terms below are small linear ramps from 30 to
// 60 years and are low confidence. Build scales the breadths.
export const ANSUR_MALE = { headLength: 0.197, headBreadth: 0.153, bizygomatic: 0.143, bigonial: 0.108, biocular: 0.092, noseBreadth: 0.036, mouthBreadth: 0.05, menVertex: 0.232 };

// frame: { vertexY, mentonY, rearZ } measured on the sculpt skin; cast: { skull: [w, h, d], jaw, lips }
export function landmarksFor({ frame, age = 35, build = 1, cast = {} }) {
  const A = ANSUR_MALE, w = (cast.skull?.[0] ?? 1) * build, ageT = Math.min(1, Math.max(0, (age - 30) / 30)), jaw = cast.jaw ?? 1;
  const V = frame.vertexY, M = frame.mentonY, Hh = V - M, Hf = 0.74 * Hh, G = M + (2 / 3) * Hf, S = M + (1 / 3) * Hf, Eye = M + 0.5 * Hh;   // thirds [LOOMIS]; face height 0.74 of head height [FARKAS, moderate]
  const zg = frame.rearZ + A.headLength * (cast.skull?.[2] ?? 1), noseP = 0.027 + 0.003 * ageT, stomY = S - 0.33 * (S - M);
  const L = [], mid = (id, y, z, note) => L.push({ id, p: [0, y, z], note }), side = (id, x, y, z, note) => L.push({ id, p: [x, y, z], mirror: true, note });
  mid("vertex", V, frame.rearZ + 0.45 * A.headLength, "top of the skull");
  mid("opisthocranion", M + 0.55 * Hh, frame.rearZ, "rear of the skull, anchors the head length");
  mid("trichion", M + Hf, zg - 0.022, "hairline, top of the face thirds");
  mid("glabella", G, zg, "brow midpoint");
  mid("nasion", G - 0.012, zg - 0.006, "root of the nose");
  mid("pronasale", S + 0.23 * (G - S), zg + noseP, "nose tip");
  mid("subnasale", S, zg + noseP - 0.02, "under the nose");
  mid("stomion", stomY, zg + 0.002, "mouth line");
  mid("pogonion", M + 0.09 * (S - M), zg - 0.006 * jaw, "chin front");
  mid("menton", M, zg - 0.016, "chin bottom");
  side("exocanthion", 0.5 * A.biocular * w, Eye, zg - 0.012, "outer eye corner");
  side("zygion", 0.5 * A.bizygomatic * w, M + 0.46 * Hh, zg - 0.03, "cheekbone apex");
  side("gonion", 0.5 * A.bigonial * w * jaw, M + 0.20 * Hh, zg - 0.09, "jaw angle");
  side("euryon", 0.5 * A.headBreadth * w, M + 0.62 * Hh, frame.rearZ + 0.45 * A.headLength, "widest skull");
  side("alare", 0.5 * A.noseBreadth * w, S + 0.08 * (G - S), zg + 0.006, "nostril wing");
  side("cheilion", 0.5 * A.mouthBreadth * w * (cast.lips ?? 1), stomY, zg - 0.016, "mouth corner");
  return L;
}
export const expandLandmarks = (L) => L.flatMap((l) => (l.mirror ? [{ ...l, id: l.id + ".r" }, { ...l, id: l.id + ".l", p: [-l.p[0], l.p[1], l.p[2]] }] : [l]));
