// Digital camera sensors from measured spectral sensitivities (options.mjs sensor.camera: the camspec
// database, Jiang et al. 2013, 28 cameras, 400 to 720 nm). A camera's raw response to scene light is
// the integral of the scene spectrum against each channel's curve; scene-linear Rec.709 is lifted to a
// spectrum with the film shader's smooth basis (film/stocks.mjs rgbBasis), so the response is a 3 x 3
// matrix. White balance divides by the response to D65-white, so a neutral stays neutral; the colour
// differences between cameras are what is left.
//
// The curves are relative (sensor, colour filter array and infrared filter together), not absolute
// quantum efficiency: they fix each camera's colour, not its noise.
import { option, registerOptionGroup } from "./options.mjs";
import { CAMERA_DATA } from "./sensor-data.mjs";

registerOptionGroup("sensor.camera", CAMERA_DATA.rows, CAMERA_DATA.prov);
import { LAMBDA, rgbBasis } from "./film/stocks.mjs";

export function cameraCurves(id) {
  const r = option("sensor.camera", id);
  const at = (arr, l) => { const i = (l - 400) / 10; return i < 0 || i > arr.length - 1 ? 0 : arr[Math.round(i)]; };
  return { id: r.id, camera: r.camera, prov: r.prov, at: (l) => [at(r.r, l), at(r.g, l), at(r.b, l)] };
}

// Row-major 3 x 3, camera RGB = M * linear Rec.709, white balanced so [1, 1, 1] -> [1, 1, 1].
export function cameraMatrix(id) {
  const cc = cameraCurves(id), basis = rgbBasis(), M = new Array(9).fill(0);
  LAMBDA.forEach((l, w) => { const s = cc.at(l); for (let k = 0; k < 3; k++) for (let c = 0; c < 3; c++) M[k * 3 + c] += s[k] * basis[w][c]; });
  for (let k = 0; k < 3; k++) { const wsum = M[k * 3] + M[k * 3 + 1] + M[k * 3 + 2]; for (let c = 0; c < 3; c++) M[k * 3 + c] /= wsum; }
  return { matrix: M, camera: cc.camera, prov: cc.prov };
}
