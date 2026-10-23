// File formats. Source files are text and diff cleanly: one stroke a line, numbers rounded to a micrometre,
// keys in a fixed order. Built meshes use RNA1, a JSON header plus aligned typed arrays (the same shape as
// the Low Water .lwmesh, so a browser reader is a page of code).
import { createHash } from "node:crypto";

export const SCULPT_FORMAT = "rnsculpt/1", ASSET_FORMAT = "rna/1";
const KEYS = ["id", "op", "shape", "c", "a", "b", "r", "R", "rot", "round", "h", "prof", "blend", "mods", "k", "mirror", "tag", "note"];
const OPS = new Set(["add", "sub", "and"]), SHAPES = new Set(["ell", "cap", "box", "tor", "rbox", "ellb", "rcone", "cone", "torx", "ext"]);
const r6 = (v) => Array.isArray(v) ? v.map(r6) : typeof v === "number" ? Math.round(v * 1e6) / 1e6 : v;
const strokeLine = (s) => "{" + KEYS.filter((k) => s[k] !== undefined && s[k] !== false).map((k) => `${JSON.stringify(k)}:${JSON.stringify(r6(s[k]))}`).join(",") + "}";

export function stringifySculpt(S) {
  const head = ["format", "name", "frame", "bounds"].map((k) => S[k] !== undefined ? `${JSON.stringify(k)}: ${JSON.stringify(r6(S[k]))}` : null).filter(Boolean);
  const eyes = (S.eyes || []).map((e) => " " + JSON.stringify(r6(e)));
  return `{\n${head.join(",\n")},\n"eyes": [\n${eyes.join(",\n")}\n],\n"strokes": [\n${S.strokes.map((s) => " " + strokeLine(s)).join(",\n")}\n]\n}\n`;
}

const isVec = (v, n = 3) => Array.isArray(v) && v.length === n && v.every(Number.isFinite);
export function validateSculpt(S) {
  const err = (m) => { throw new Error(`rnsculpt: ${m}`); };
  if (S.format !== SCULPT_FORMAT) err(`format is "${S.format}", expected "${SCULPT_FORMAT}"`);
  if (!Array.isArray(S.strokes)) err("strokes must be an array");
  const seen = new Set();
  for (const s of S.strokes) {
    if (!s.id || seen.has(s.id)) err(`stroke id missing or repeated: "${s.id}"`);
    seen.add(s.id);
    if (!OPS.has(s.op)) err(`stroke ${s.id}: op "${s.op}"`);
    if (!SHAPES.has(s.shape)) err(`stroke ${s.id}: shape "${s.shape}"`);
    if (s.k !== undefined && !(Number.isFinite(s.k) && s.k >= 0)) err(`stroke ${s.id}: k must be a number >= 0`);
    if (s.blend !== undefined && s.blend !== "quad" && s.blend !== "cubic") err(`stroke ${s.id}: blend must be quad or cubic`);
    if (s.mods !== undefined && !(Array.isArray(s.mods) && s.mods.every((m) => m && typeof m.kind === "string"))) err(`stroke ${s.id}: mods must be a list of { kind }`);
    const need = ["cap", "rcone", "cone"].includes(s.shape) ? ["a", "b"] : ["c"];
    if ((s.shape === "rbox" || s.shape === "ellb") && (!isVec(s.r) || s.r.some((v) => v <= 0))) err(`stroke ${s.id}: r must be 3 positive numbers`);
    if (s.shape === "torx" && !(s.R > 0 && s.r > 0)) err(`stroke ${s.id}: R and r must be positive`);
    if ((s.shape === "rcone" || s.shape === "cone") && !(Number.isFinite(s.r) || isVec(s.r, 2))) err(`stroke ${s.id}: r must be a number or two`);
    if (s.shape === "ext" && !(s.h > 0 && s.prof && s.prof.kind)) err(`stroke ${s.id}: ext needs h > 0 and prof { kind }`);
    for (const key of need) if (!isVec(s[key])) err(`stroke ${s.id}: ${key} must be 3 numbers`);
    if (s.shape === "ell" || s.shape === "box") { if (!isVec(s.r) || s.r.some((v) => v <= 0)) err(`stroke ${s.id}: r must be 3 positive numbers`); }
    if (s.shape === "cap" && !(Number.isFinite(s.r) || isVec(s.r, 2))) err(`stroke ${s.id}: r must be a number or two`);
    if (s.shape === "tor" && !(s.R > 0 && s.r > 0)) err(`stroke ${s.id}: R and r must be positive`);
    if (s.rot !== undefined && !isVec(s.rot)) err(`stroke ${s.id}: rot must be 3 numbers`);
  }
  return S;
}
export const parseSculpt = (text) => validateSculpt(JSON.parse(text));
export const sha256 = (data) => createHash("sha256").update(data).digest("hex");

// ---- RNA1: header length, JSON header, then each array padded to 4 bytes -------------------------------
const TYPES = { f32: Float32Array, u32: Uint32Array, u16: Uint16Array, u8: Uint8Array };
const typeOf = (a) => a instanceof Float32Array ? "f32" : a instanceof Uint32Array ? "u32" : a instanceof Uint16Array ? "u16" : a instanceof Uint8Array ? "u8" : null;
const pad4 = (n) => (n + 3) & ~3;

export function packAsset(meta, arrays) {
  const names = Object.keys(arrays).sort(), list = names.map((n) => { const t = typeOf(arrays[n]); if (!t) throw new Error(`rna: array ${n} is not a typed array`); return { name: n, type: t, length: arrays[n].length }; });
  const hb = new TextEncoder().encode(JSON.stringify({ format: ASSET_FORMAT, meta, arrays: list })), size = 8 + pad4(hb.length) + list.reduce((s, a) => s + pad4(arrays[a.name].byteLength), 0);
  const buf = new ArrayBuffer(size), dv = new DataView(buf), u8 = new Uint8Array(buf);
  u8.set([82, 78, 65, 49], 0); dv.setUint32(4, hb.length, true); u8.set(hb, 8);
  let off = 8 + pad4(hb.length);
  for (const a of list) { const src = arrays[a.name]; u8.set(new Uint8Array(src.buffer, src.byteOffset, src.byteLength), off); off += pad4(src.byteLength); }
  return buf;
}
export function unpackAsset(buf) {
  const u8 = new Uint8Array(buf);
  if (String.fromCharCode(...u8.subarray(0, 4)) !== "RNA1") throw new Error("rna: not an RNA1 file");
  const hl = new DataView(buf instanceof ArrayBuffer ? buf : buf.buffer, u8.byteOffset + 4).getUint32(0, true), head = JSON.parse(new TextDecoder().decode(u8.subarray(8, 8 + hl)));
  const arrays = {}; let off = 8 + pad4(hl);
  for (const a of head.arrays) { const T = TYPES[a.type], bytes = a.length * T.BYTES_PER_ELEMENT, copy = new Uint8Array(bytes); copy.set(u8.subarray(off, off + bytes)); arrays[a.name] = new T(copy.buffer); off += pad4(bytes); }
  return { meta: head.meta, arrays };
}
