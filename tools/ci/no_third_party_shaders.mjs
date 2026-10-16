// Fails when a third-party shader source, licence header or package import appears under
// web/shaders (author rule, 2026-10-15: all effect code is original, no shader libraries).
//   node tools/ci/no_third_party_shaders.mjs [--root web/shaders] [--allow tools/ci/third_party_allowlist.json]
// A finding is one of:
//   licence   a licence or ownership line: GPL family, MPL, Apache, BSD, Creative Commons,
//             "Permission is hereby granted", SPDX-License-Identifier, "All rights reserved",
//             "Licensed under", or a Copyright line whose owner is not the project's author;
//   import    an import, require or #include of something other than a relative path or node:;
//   origin    a header (first 40 lines) or an include that names a known third-party shader
//             source (ReShade.fxh, ENBSeries, Shadertoy, named effect authors).
// Lines longer than 300 characters (embedded data) are not read. A file may be allowed only by
// an entry in the allowlist, which carries a reason, so an exception is visible in review.
import { readdirSync, readFileSync, statSync, existsSync } from "node:fs";
import { join, relative, extname } from "node:path";

const EXT = new Set([".mjs", ".js", ".wgsl", ".hlsl", ".glsl", ".fx", ".fxh", ".html", ".css"]);
const LICENCE = [
  /GNU (Lesser |Affero )?General Public License/i, /\b(A|L)?GPL(-?v?\d(\.\d)?)?\b/, /Mozilla Public License|MPL-?2/i, /Apache License|Apache-2/i, /\bBSD[- ]?(2|3)?-?Clause|\bBSD licen[cs]e/i,
  /Creative Commons|\bCC[- ]BY\b|\bCC0\b/i, /Permission is hereby granted/i, /SPDX-License-Identifier/i, /All rights reserved/i, /Licensed under/i,
];
const OWN = /Zain Dana Harper|HarperZ9/i;
const ORIGIN = /ReShade\.fxh|\bReShade\b|ENBSeries|enbhelper|Boris Vorontsov|Pascal Gilcher|shadertoy\.com|Inigo Quilez|iquilezles|crt-royale|crt-guest|libretro|Atrix256|Dither3D|runevision/i;

// The module specifier of an import, export-from, dynamic import, require or #include on this line, else null.
export function importSpecifier(line) {
  const t = line.trim();
  if (t.startsWith("//") || t.startsWith("*") || t.startsWith("/*")) return null;
  let m = /^(?:import|export)\b[^'"`]*?\bfrom\s*(['"])([^'"]+)\1/.exec(t) || /^import\s*(['"])([^'"]+)\1/.exec(t);
  if (m) return m[2];
  m = /(?:^|[=(,;{}\s])(?:await\s+)?(?:import|require)\(\s*(['"])([^'"]+)\1\s*\)/.exec(t);
  if (m) return m[2];
  m = /^#include\s*[<"]([^>"]+)[>"]/.exec(t) || /^@import\s*(?:url\()?\s*['"]([^'"]+)['"]/.exec(t);
  return m ? m[1] : null;
}

export function scan(root, allow = []) {
  const found = [], allowed = new Map(allow.map((a) => [a.path, a.reason]));
  const walk = (dir) => {
    for (const name of readdirSync(dir)) {
      const p = join(dir, name), st = statSync(p);
      if (st.isDirectory()) { walk(p); continue; }
      if (!EXT.has(extname(name))) continue;
      const rel = relative(root, p).split("\\").join("/");
      if (allowed.has(rel)) continue;
      const lines = readFileSync(p, "utf8").split(/\r?\n/);
      lines.forEach((line, i) => {
        if (line.length > 300) return;
        const at = (kind, text) => found.push({ file: rel, line: i + 1, kind, text: text.trim().slice(0, 120) });
        if (LICENCE.some((r) => r.test(line))) at("licence", line);
        else if (/Copyright\b/i.test(line) && !OWN.test(line)) at("licence", line);
        const spec = importSpecifier(line);
        if (spec !== null && !/^(\.{1,2}\/|node:)/.test(spec)) at("import", line);
        if ((i < 40 || /#include|@import|^\s*import\b/.test(line)) && ORIGIN.test(line)) at("origin", line);
      });
    }
  };
  walk(root);
  return found;
}

if ((process.argv[1] || "").replace(/\\/g, "/").endsWith("tools/ci/no_third_party_shaders.mjs")) {
  const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
  const root = opt("--root", "web/shaders"), allowPath = opt("--allow", "tools/ci/third_party_allowlist.json");
  const allow = existsSync(allowPath) ? JSON.parse(readFileSync(allowPath, "utf8")) : [];
  for (const a of allow) if (!a.path || !a.reason) { console.error(`allowlist entry needs path and reason: ${JSON.stringify(a)}`); process.exit(2); }
  const found = scan(root, allow);
  for (const f of found) console.error(`${f.kind}: ${root}/${f.file}:${f.line}: ${f.text}`);
  console.log(`no_third_party_shaders: ${found.length} finding(s) under ${root}, ${allow.length} allowlisted file(s)`);
  process.exit(found.length ? 1 : 0);
}
