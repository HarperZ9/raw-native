// Runs one wasm variant's benchmark matrix inside a Worker and posts the
// results. Message in: { variant, moduleUrl, wasmUrl, wasmSha256, moduleSha256,
// threads, sizes, modes, runs }.
import { loadRawNative } from "../wasm/raw-loader.mjs";

self.onmessage = async ({ data }) => {
  try {
    const raw = await loadRawNative({
      moduleUrl: data.moduleUrl, moduleSha256: data.moduleSha256,
      wasmUrl: data.wasmUrl, wasmSha256: data.wasmSha256,
      importDirect: data.variant === "wasm-threads",
    });
    const results = [];
    for (const [width, height] of data.sizes) {
      for (const rt of data.modes) {
        const r = raw.bench({ width, height, rt, threads: data.threads }, data.runs);
        results.push({ variant: data.variant, ...r });
        self.postMessage({ progress: `${data.variant} ${width}x${height} rt=${rt} median ${r.median_ms} ms` });
      }
    }
    self.postMessage({ done: true, version: raw.version, results });
  } catch (e) {
    self.postMessage({ error: String(e && e.stack || e) });
  }
};
