// node web/shaders/cascades3d/export-scene.mjs NAME OUT.bin: a synthetic scene (scene3d.mjs) as a
// scene file, for raw_native_cli gi-reference --scene-file.
import { writeFileSync } from "node:fs";
import { buildScene3d, writeScene } from "./scene3d.mjs";

const [name, out] = process.argv.slice(2);
if (!name || !out) { console.error("usage: node export-scene.mjs NAME OUT.bin"); process.exit(2); }
writeFileSync(out, writeScene(buildScene3d(name)));
