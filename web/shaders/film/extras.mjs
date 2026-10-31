// Exposure options that follow from light adding linearly, and the list of physical film options that wait on data.
//
// Film integrates light: before development, exposure is additive in linear scene light (reciprocity failure
// aside, which the sheets read here put outside 1/10000 to 1 s for the motion-picture stocks). So a double exposure and
// a trichrome separation are operations on linear scenes, done before createFilm().frame() and needing no new numbers.
// Each scene is { width, height, data } with four channels per pixel.

function sameSize(scenes) {
  if (!scenes.length) throw new Error("no scenes");
  const { width, height } = scenes[0];
  for (const s of scenes) if (s.width !== width || s.height !== height) throw new Error("scenes differ in size");
  return { width, height, n: width * height };
}

// Multiple exposure: the frames' light added on one piece of film. evs: stops of exposure for each frame (default 0).
export function multipleExposure(scenes, evs = []) {
  const { width, height, n } = sameSize(scenes), data = new Float32Array(n * 4);
  scenes.forEach((s, k) => {
    const g = Math.pow(2, evs[k] ?? 0);
    for (let i = 0; i < n; i++) for (let c = 0; c < 3; c++) data[i * 4 + c] += g * s.data[i * 4 + c];
  });
  for (let i = 0; i < n; i++) data[i * 4 + 3] = 1;
  return { width, height, data };
}

// Trichrome: three exposures made at different moments through red, green and blue filters, the look of the
// Prokudin-Gorskii separations and of Harris-shutter colour. The filters are taken as the scene's own linear Rec.709
// channels (status defined: no filter transmission is measured here), so what moves between frames fringes in colour.
export function trichrome(red, green, blue) {
  const { width, height, n } = sameSize([red, green, blue]), data = new Float32Array(n * 4);
  for (let i = 0; i < n; i++) {
    data[i * 4] = red.data[i * 4]; data[i * 4 + 1] = green.data[i * 4 + 1]; data[i * 4 + 2] = blue.data[i * 4 + 2]; data[i * 4 + 3] = 1;
  }
  return { width, height, data };
}

// Options considered and what stands between each and an honest implementation. An entry leaves this list
// only with its measured numbers. "implemented" points at the code above.
export const CANDIDATES = {
  "multiple-exposure": { status: "implemented", in: "multipleExposure", why: "exposure adds in linear light; needs no new data" },
  trichrome: { status: "implemented", in: "trichrome", why: "three exposures combined per channel; filters idealised to Rec.709, status defined" },
  redscale: { status: "not implemented", why: "light entering through the base crosses the layers in reverse; the yellow filter layer's and the undercoat's densities are not published for any stock held" },
  "base-side-light-leak": { status: "not implemented", why: "the halation ring gives the base's light piping, but no measurement of a leak's entry geometry or spectrum is held" },
  "aerochrome-infrared": { status: "not implemented", why: "Kodak's Aerochrome III 1443 sheet gives no curves in any source fetched; the camspec curves stop at 720 nm and the shader's spectra at 780 nm" },
  "hue-swap-stocks": { status: "not implemented", why: "LomoChrome-style stocks publish no characteristic curves or dye spectra" },
  "film-ageing": { status: "not implemented", why: "no published fog-and-dye-fade series per stock is held; Kodak's sheets state storage conditions, not rates" },
  "wet-plate-collodion": { status: "not implemented", why: "no measured collodion sensitivity or characteristic curve is held; ILFORD ORTHO PLUS is the nearest measured curve and is a different process" },
};
