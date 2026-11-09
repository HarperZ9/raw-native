// Exports. glTF 2.0 with embedded base64 buffers is what raw-native's loader (src/assets/gltf.cpp) reads;
// OBJ is for any other tool; the Low Water browser mesh is written by that game's own adapter from the
// RNA1 arrays, because material ids and joint weights are the game's.
import { encodePng } from "./png.mjs";
import { triQuads } from "./build.mjs";

const b64 = (u8) => Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength).toString("base64");
export function toGltf(built, { name = "head" } = {}) {
  const { U, tex, res } = built, pos = U.pos, nrm = U.nrm, uv = U.uv, idx = triQuads(U.quads), bin = Buffer.concat([pos, nrm, uv, idx].map((a) => Buffer.from(a.buffer, a.byteOffset, a.byteLength)));
  let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; for (let v = 0; v < pos.length; v += 3) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], pos[v + k]); hi[k] = Math.max(hi[k], pos[v + k]); }
  const png = (data, channels) => "data:image/png;base64," + encodePng({ width: res, height: res, channels, data }).toString("base64");
  const o0 = 0, o1 = pos.byteLength, o2 = o1 + nrm.byteLength, o3 = o2 + uv.byteLength;
  return JSON.stringify({
    asset: { version: "2.0", generator: "raw-native tools/asset" }, scene: 0, scenes: [{ nodes: [0] }], nodes: [{ mesh: 0, name }],
    meshes: [{ name, primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3, material: 0 }] }],
    materials: [{ name: "skin", pbrMetallicRoughness: { baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: 0.8 }, normalTexture: { index: 1 } }],
    textures: [{ source: 0 }, { source: 1 }], images: [{ uri: png(tex.albedo, 3) }, { uri: png(tex.normal, 3) }],
    buffers: [{ byteLength: bin.length, uri: "data:application/octet-stream;base64," + bin.toString("base64") }],
    bufferViews: [{ buffer: 0, byteOffset: o0, byteLength: pos.byteLength, target: 34962 }, { buffer: 0, byteOffset: o1, byteLength: nrm.byteLength, target: 34962 }, { buffer: 0, byteOffset: o2, byteLength: uv.byteLength, target: 34962 }, { buffer: 0, byteOffset: o3, byteLength: idx.byteLength, target: 34963 }],
    accessors: [{ bufferView: 0, componentType: 5126, count: pos.length / 3, type: "VEC3", min: lo, max: hi }, { bufferView: 1, componentType: 5126, count: nrm.length / 3, type: "VEC3" }, { bufferView: 2, componentType: 5126, count: uv.length / 2, type: "VEC2" }, { bufferView: 3, componentType: 5125, count: idx.length, type: "SCALAR" }],
  });
}
export function toObj(U, name = "head") {
  const out = [`o ${name}`]; for (let v = 0; v < U.pos.length; v += 3) out.push(`v ${U.pos[v].toFixed(6)} ${U.pos[v + 1].toFixed(6)} ${U.pos[v + 2].toFixed(6)}`);
  for (let v = 0; v < U.uv.length; v += 2) out.push(`vt ${U.uv[v].toFixed(6)} ${(1 - U.uv[v + 1]).toFixed(6)}`);
  for (let v = 0; v < U.nrm.length; v += 3) out.push(`vn ${U.nrm[v].toFixed(5)} ${U.nrm[v + 1].toFixed(5)} ${U.nrm[v + 2].toFixed(5)}`);
  for (let q = 0; q < U.quads.length; q += 4) out.push("f " + [0, 1, 2, 3].map((e) => { const i = U.quads[q + e] + 1; return `${i}/${i}/${i}`; }).join(" "));
  return out.join("\n") + "\n";
}
