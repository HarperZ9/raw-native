// The colonnade's live settings panel (evidence/colonnade-panel-bounds.json): every change runs the
// engine's own `colonnade` command in its WebAssembly build, a 2-sample pass and then a finer one.
// window.__colonnade records every run for tests/web/colonnade_panel.py; window.__setSettings sets
// the controls and renders.
import { loadRawNative } from "../../wasm/raw-loader.mjs";

const $ = (id) => document.getElementById(id);
const W = 192, H = 108;
const state = { runs: [], ready: false, error: null };
window.__colonnade = state;
let raw = null, generation = 0;

async function loadModule() {
  const q = new URLSearchParams(location.search), dir = q.get("dir") || "build-wasm";
  let m = q.get("m"), w = q.get("w");
  if (!m || !w) {
    const sums = await (await fetch("../../build-wasm-sums.json")).json();
    m = sums.wasm.mjs; w = sums.wasm.wasm;
  }
  return loadRawNative({ moduleUrl: `../../${dir}/raw-native.mjs`, moduleSha256: m, wasmUrl: `../../${dir}/raw-native.wasm`, wasmSha256: w });
}

function settings() {
  return { elev: +$("elev").value, az: +$("az").value, turbidity: +$("turb").value, ev: +$("ev").value, spp: +$("spp").value, emission: $("emission").checked };
}
for (const id of ["elev", "az", "turb", "ev", "spp"]) $(id).addEventListener("input", () => { $(`${id}-v`).textContent = $(id).value; });
for (const id of ["elev", "az", "turb", "ev", "spp", "emission"]) $(id).addEventListener("change", () => render(settings()));

// The mean 8-bit luminance of a PNG, read back through an image and a canvas (for the test's l3).
async function meanLuminance(url) {
  const img = new Image();
  img.src = url;
  await img.decode();
  const c = Object.assign(document.createElement("canvas"), { width: img.width, height: img.height });
  const g = c.getContext("2d");
  g.drawImage(img, 0, 0);
  const px = g.getImageData(0, 0, img.width, img.height).data;
  let s = 0;
  for (let i = 0; i < px.length; i += 4) s += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
  return s / (px.length / 4);
}

function runOnce(s, spp) {
  const dir = `/colonnade-${state.runs.length}`;
  const args = ["colonnade", "--out", dir, "--width", String(W), "--height", String(H), "--spp", String(spp), "--bounces", "6",
    "--sun-elev", String(s.elev), "--sun-az", String(s.az), "--turbidity", String(s.turbidity), "--ev", String(s.ev), "--threads", "1"];
  if (!s.emission) args.push("--no-emission");
  const t0 = performance.now();
  const { exitCode, stdout } = raw.runCli(args);
  const ms = performance.now() - t0;
  const fs = raw.module.FS, read = (n) => { try { return fs.readFile(`${dir}/${n}`); } catch { return null; } };
  const png = read("colonnade.png"), js = read("colonnade.json");
  for (const n of ["colonnade.png", "colonnade.pfm", "colonnade.json"]) { try { fs.unlink(`${dir}/${n}`); } catch { /* absent */ } }
  try { fs.rmdir(dir); } catch { /* not empty or absent */ }
  return { exitCode, stdout, ms, png, json: js ? JSON.parse(new TextDecoder().decode(js)) : null };
}

async function render(s) {
  if (!raw) return;
  const mine = ++generation;
  for (const spp of [2, s.spp]) {
    $("status").textContent = `rendering ${spp} samples a pixel`;
    await new Promise((r) => setTimeout(r, 0));   // let the page paint before the blocking run
    if (mine !== generation) return;              // a newer change took over
    const r = runOnce(s, spp);
    let url = null, mean = null;
    if (r.png) {
      url = URL.createObjectURL(new Blob([r.png], { type: "image/png" }));
      $("frame").src = url;
      mean = await meanLuminance(url);
    }
    const j = r.json;
    $("caption").textContent = j ? `sun ${j.sun_elevation} deg at ${j.sun_azimuth}, turbidity ${j.turbidity}, EV ${j.ev100}, ${j.spp} spp: traced in ${j.trace_ms.toFixed(0)} ms (${r.ms.toFixed(0)} ms in the page)` : `exit ${r.exitCode}: ${r.stdout}`;
    state.runs.push({ settings: { ...s }, spp, exitCode: r.exitCode, hasPng: !!r.png, json: j, pageMs: r.ms, meanLuminance: mean });
  }
  $("status").textContent = "ready";
}

window.__setSettings = async (s) => {
  for (const [k, id] of [["elev", "elev"], ["az", "az"], ["turbidity", "turb"], ["ev", "ev"], ["spp", "spp"]]) if (s[k] !== undefined) { $(id).value = s[k]; $(`${id}-v`).textContent = s[k]; }
  if (s.emission !== undefined) $("emission").checked = s.emission;
  await render(settings());
};

try {
  raw = await loadModule();
  await render(settings());
  state.ready = true;
} catch (e) {
  state.error = String(e);
  $("status").textContent = `could not start: ${e}`;
}
