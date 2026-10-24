// A structural fingerprint of WGSL source, for checking that a rewrite is a
// different algorithm and not the old one renamed. Identifiers are folded to
// one token, while keywords, builtins, numbers and operators stay, and the
// fingerprint is the set of FNV-1a hashes of every run of SHINGLE tokens.
// Renaming variables or functions therefore leaves it unchanged.

export const SHINGLE = 8;

// Functions the resynthesis keeps on purpose: the worlds and their palettes,
// and the bloom pyramid's storage access. The method comparison leaves them out.
export const KEPT = /^(rotv|fmodf|seg|ring|box2|figure|f[A-Z]\w*|puppetString|prob|palette|simV|form|texel|bilin)$/;

const WORDS = new Set(("fn let var const struct return if else for loop break continue select true false " +
  "f32 u32 i32 bool vec2f vec3f vec4f vec2u vec3u vec4u array atomic ptr function storage uniform read " +
  "read_write workgroup group binding compute workgroup_size builtin global_invocation_id " +
  "local_invocation_id sin cos tan atan atan2 exp log log2 exp2 sqrt pow abs min max clamp floor ceil " +
  "fract mix step smoothstep length dot normalize cross atomicAdd workgroupBarrier").split(" "));

export function tokens(src) {
  const text = src.replace(/\/\/[^\n]*/g, " ");
  const out = [];
  for (const m of text.matchAll(/[A-Za-z_]\w*|\d+\.?\d*(?:e-?\d+)?[uif]?|0x[0-9a-fA-F]+u?|[-+*/%<>=!&|^]=?|->|[(){}[\],;:.]/g)) {
    const t = m[0];
    out.push(/^[A-Za-z_]/.test(t) && !WORDS.has(t) ? "ID" : t);
  }
  return out;
}

function fnv(s) {
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
  return h;
}

export function fingerprint(src) {
  const t = tokens(src), set = new Set();
  for (let i = 0; i + SHINGLE <= t.length; i++) set.add(fnv(t.slice(i, i + SHINGLE).join(" ")));
  return [...set].sort((a, b) => a - b);
}

export function jaccard(a, b) {
  const A = new Set(a);
  let both = 0;
  for (const x of b) if (A.has(x)) both++;
  return both / (A.size + new Set(b).size - both || 1);
}

// The text of one pass ("//@pass name" to the next marker), with the kept
// world and palette functions removed.
export function methodOf(src, pass) {
  const parts = src.replace(/\r\n/g, "\n").split(/^\/\/@pass (\w+)[^\S\n]*\n/m);
  const i = parts.indexOf(pass);
  if (i < 1) throw new Error(`no pass ${pass}`);
  return parts[i + 1].replace(/^fn (\w+)\([^\n]*\{\n(?:[^\n]*\n)*?\}\n/gm, (blk, name) => (KEPT.test(name) ? "" : blk))
    .replace(/^fn (\w+)\([^\n]*\}\n/gm, (line, name) => (KEPT.test(name) ? "" : line));
}
