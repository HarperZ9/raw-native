#!/usr/bin/env node
// asset build <name.sculpt.json> <name.paint.json> <outDir> [--res N] [--quick]   build everything, write review.png and bounds.json
// asset fmt <file.sculpt.json>                                                       rewrite in canonical form (one stroke a line)
// asset review <name.sculpt.json> <name.paint.json> <out.png>                        quick sheet only (coarser maps)
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join, basename } from "node:path";
import { parseSculpt, stringifySculpt } from "../src/format.mjs";
import { parsePaint } from "../src/paint.mjs";
import { buildAsset, reviewSheet, LODS } from "../src/build.mjs";
import { encodePng } from "../src/png.mjs";
import { savePng } from "../src/render.mjs";
import { toGltf, toObj } from "../src/export.mjs";

const [cmd, ...rest] = process.argv.slice(2), flag = (n, d) => { const i = rest.indexOf(n); return i < 0 ? d : (rest[i + 1] && !rest[i + 1].startsWith("--") ? rest[i + 1] : true); };
const args = rest.filter((a, i) => !a.startsWith("--") && !(rest[i - 1] || "").startsWith("--"));
const log = (m) => console.error(m);
if (cmd === "fmt") { const f = args[0], S = parseSculpt(readFileSync(f, "utf8")); writeFileSync(f, stringifySculpt(S)); console.log(`${f}: ${S.strokes.length} strokes`); }
else if (cmd === "build" || cmd === "review") {
  const [sf, pf, out] = args, sculpt = parseSculpt(readFileSync(sf, "utf8")), paint = parsePaint(readFileSync(pf, "utf8")), quick = cmd === "review" || flag("--quick", false);
  const res = +flag("--res", quick ? 512 : 1024), built = buildAsset({ sculpt, paint, res, refCell: quick ? 0 : 0.0015, lods: quick ? LODS.slice(0, 2) : LODS, log });
  if (cmd === "review") writeFileSync(out, savePng(reviewSheet(built)));
  else {
    mkdirSync(out, { recursive: true }); const name = basename(sf).replace(/\.sculpt\.json$/, "");
    writeFileSync(join(out, `${name}.rna`), Buffer.from(built.rna)); writeFileSync(join(out, "review.png"), savePng(reviewSheet(built)));
    const png = (k, ch, data) => writeFileSync(join(out, `${name}.${k}.png`), encodePng({ width: res, height: res, channels: ch, data }));
    png("albedo", 3, built.tex.albedo); png("normal", 3, built.tex.normal); png("ao", 1, built.tex.ao); png("curv", 1, built.tex.curv); png("thick", 1, built.tex.thick);
    writeFileSync(join(out, `${name}.gltf`), toGltf(built, { name })); writeFileSync(join(out, `${name}.obj`), toObj(built.U, name));
    writeFileSync(join(out, "bounds.json"), JSON.stringify(built.bounds, null, 1) + "\n");
  }
  console.log(JSON.stringify({ lods: built.bounds.lods.map((l) => [l.name, l.tris, +(l.error.max * 1000).toFixed(3)]), uv: built.bounds.uv.stretch, bake: built.bounds.bake, ms: built.bounds.timingMs }));
} else { console.error("usage: asset build|review|fmt ..."); process.exit(2); }
