// Digital camera sensors from measured spectral sensitivities (options.mjs sensor.camera: the camspec
// database, Jiang et al. 2013, 28 cameras, 400 to 720 nm). A camera's raw response to scene light is
// the integral of the scene spectrum against each channel's curve; scene-linear Rec.709 is lifted to a
// spectrum with the film shader's smooth basis (film/stocks.mjs rgbBasis), so the response is a 3 x 3
// matrix, integrated when the data file is generated (tools/shaders/film_library.mjs); the curves themselves
// stay in the private measured table. White balance divides by the response to D65-white, so a neutral stays
// neutral; the colour differences between cameras are what is left.
//
// The curves are relative (sensor, colour filter array and infrared filter together), not absolute
// quantum efficiency: they fix each camera's colour, not its noise.
import { option, registerOptionGroup } from "./options.mjs";
import { CAMERA_DATA } from "./sensor-data.mjs";

registerOptionGroup("sensor.camera", CAMERA_DATA.rows, CAMERA_DATA.prov);

// Row-major 3 x 3, camera RGB = M * linear Rec.709, white balanced so [1, 1, 1] -> [1, 1, 1].
export function cameraMatrix(id) {
  const r = option("sensor.camera", id);
  return { matrix: r.matrix, camera: r.camera, prov: r.prov };
}
