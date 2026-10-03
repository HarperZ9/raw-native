// Run the WebAssembly build under Node and copy its outputs to a real directory.
// Usage: node wasm/run-node.mjs <module.mjs> <outdir> [raw_native_cli flags...]
import { mkdirSync, writeFileSync } from "node:fs";
import { join, posix, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [modulePath, outDir, ...flags] = process.argv.slice(2);
if (!modulePath || !outDir) {
  console.error("usage: node wasm/run-node.mjs <module.mjs> <outdir> [flags...]");
  process.exit(2);
}
const { default: createRawNative } = await import(pathToFileURL(resolve(modulePath)).href);
const lines = [];
const mod = await createRawNative({ print: (s) => lines.push(s), printErr: (s) => console.error(s) });
mod.FS.mkdir("/out");
const rc = mod.callMain(["--out", "/out", ...flags]);
console.log(lines.join("\n"));
mkdirSync(outDir, { recursive: true });
for (const name of mod.FS.readdir("/out")) {
  if (name === "." || name === "..") continue;
  writeFileSync(join(outDir, name), mod.FS.readFile(posix.join("/out", name)));
}
process.exit(rc);
