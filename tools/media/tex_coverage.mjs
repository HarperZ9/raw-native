// Which equations in a set of Motion scenes the engine's TeX subset cannot render
// (M1 exit criterion 5).
//
//   node tools/media/tex_coverage.mjs --scenes DIR [--scenes DIR ...] --atlas FILE [--atlas FILE ...] [--out evidence.json]
//
// It reads every .mjs file under each scene directory. An equation is a string or
// template literal that holds a TeX command (a backslash and a letter) and is passed
// to tex() or to a function that calls it. Each ${...} is replaced by a sample number
// (1{,}234.5), since live parameters change the digits only. Each equation is laid
// out with web/motion/text.mjs tex() against each atlas. The tool reports:
// - unsupported commands (tex() throws);
// - characters with no glyph in an atlas (tex() would draw the fallback "?").
// Exit 1 when either list is not empty.
import { readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { tex, SYMBOLS, hasGlyph } from "../../web/motion/text.mjs";

// Each --atlas applies to the --scenes before it: the atlas those scenes set equations in.
const args = process.argv.slice(2), scenes = [];
let out = null;
for (let i = 0; i < args.length; i++) {
  if (args[i] === "--scenes") scenes.push({ dir: args[++i], atlases: [] });
  else if (args[i] === "--atlas" && scenes.length) scenes[scenes.length - 1].atlases.push(args[++i]);
  else if (args[i] === "--out") out = args[++i];
}
if (!scenes.length || scenes.some((s) => !s.atlases.length)) { console.error("usage: tex_coverage.mjs (--scenes DIR --atlas FILE ...)+ [--out FILE]"); process.exit(2); }

const slash = (p) => p.split(String.fromCharCode(92)).join("/");
const files = (d) => readdirSync(d).flatMap((f) => { const p = join(d, f); return statSync(p).isDirectory() ? files(p) : p.endsWith(".mjs") ? [p] : []; });
// String and template literals in source order, skipping comments. Template
// substitutions ${...} (with nested braces) become a sample number.
function literals(src) {
  const out = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === "/" && src[i + 1] === "/") { i = src.indexOf("\n", i); if (i < 0) break; continue; }
    if (c === "/" && src[i + 1] === "*") { i = src.indexOf("*/", i + 2); if (i < 0) break; i += 2; continue; }
    if (c === '"' || c === "'" || c === "`") {
      const start = i;
      let s = "";
      i++;
      while (i < src.length && src[i] !== c) {
        if (src[i] === "\\") { s += src[i + 1]; i += 2; continue; }
        if (c === "`" && src[i] === "$" && src[i + 1] === "{") {
          let depth = 1; i += 2;
          while (i < src.length && depth) { if (src[i] === "{") depth++; else if (src[i] === "}") depth--; i++; }
          s += "1{,}234.5";
          continue;
        }
        s += src[i++];
      }
      i++;
      out.push({ s, index: start });
      continue;
    }
    i++;
  }
  return out;
}

const found = [];
for (const { dir, atlases } of scenes) for (const f of files(dir)) {
  const src = readFileSync(f, "utf8");
  if (!/\btex\(/.test(src)) continue;
  for (const m of literals(src)) {
    const s = m.s;
    if (!/\\[A-Za-z]/.test(s)) continue;
    const line = src.slice(0, m.index).split("\n").length;
    found.push({ file: slash(relative(process.cwd(), f)), line, tex: s, atlases });
  }
}

// The characters an equation draws: symbol commands by their glyph, other
// commands (\text, \frac, spacing) by nothing, braces and scripts by nothing.
function drawn(t) {
  const chars = [];
  for (let i = 0; i < t.length; i++) {
    if (t[i] === "\\") {
      const m = /^\\([A-Za-z]+|.)/.exec(t.slice(i));
      if (SYMBOLS[m[1]]) chars.push(SYMBOLS[m[1]]);
      i += m[0].length - 1;
    } else if (!"{}^_ ".includes(t[i])) chars.push(t[i]);
  }
  return chars;
}

const loaded = new Map();
const load = (p) => { if (!loaded.has(p)) loaded.set(p, { path: slash(p), a: JSON.parse(readFileSync(p, "utf8")) }); return loaded.get(p); };
const unsupported = new Set(), missing = new Set();
for (const e of found) {
  e.ok = true;
  for (const { path, a } of e.atlases.map(load)) {
    try {
      tex(e.tex, { atlas: a, size: 44 });
      const miss = [...new Set(drawn(e.tex))].filter((ch) => !hasGlyph(a, ch));
      if (miss.length) { e.ok = false; e.missing = { ...(e.missing || {}), [path]: miss }; miss.forEach((c) => missing.add(c)); }
    } catch (err) {
      e.ok = false; e.error = String(err.message);
      const cmd = /unsupported command (\\\S+)/.exec(err.message);
      if (cmd) unsupported.add(cmd[1]);
    }
  }
}
const res = { criterion: "M1 exit criterion 5: equations render from the engine's TeX subset", equations: found.length,
  unsupported_commands: [...unsupported], missing_glyphs: [...missing], atlases: [...new Set(scenes.flatMap((s) => s.atlases))].map(slash), list: found.map(({ atlases, ...e }) => e),
  does_not_prove: "That every equation in the films is found: only literals in scene modules that contain a TeX command are read." };
console.log(JSON.stringify(res, null, 1));
if (out) writeFileSync(out, JSON.stringify(res, null, 1) + "\n");
process.exit(unsupported.size || missing.size ? 1 : 0);
