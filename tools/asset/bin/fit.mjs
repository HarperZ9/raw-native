#!/usr/bin/env node
// fit <name.sculpt.json> <fit.json> <out.sculpt.json>   fit the sculpt to anthropometric landmarks and write the fitted sculpt plus a report
// fit.json: { age, build, cast: { skull, jaw, lips }, frame: { vertexY, mentonY, rearZ }, free: [stroke ids], tol, extra: [{ id, p, mirror }] }
import { readFileSync, writeFileSync } from "node:fs";
import { parseSculpt, stringifySculpt } from "../src/format.mjs";
import { landmarksFor, expandLandmarks } from "../src/anthro.mjs";
import { fitLandmarks } from "../src/fit.mjs";

const [sf, ff, out] = process.argv.slice(2), sculpt = parseSculpt(readFileSync(sf, "utf8")), spec = JSON.parse(readFileSync(ff, "utf8"));
const keep = (l) => !spec.only || spec.only.some((p) => l.id.startsWith(p)), L = expandLandmarks([...landmarksFor({ frame: spec.frame, age: spec.age, build: spec.build, cast: spec.cast }), ...(spec.extra || [])].filter(keep));
const R = fitLandmarks(sculpt.strokes, L, { free: spec.free || [], rigs: spec.rigs || [], tol: spec.tol ?? 0.002, lambda: spec.lambda ?? 1 });
writeFileSync(out, stringifySculpt({ ...sculpt, strokes: R.strokes }));
const mm = (v) => +(v * 1000).toFixed(2), rows = R.before.map((b, i) => ({ id: b.id, beforeMm: mm(b.err), afterMm: mm(R.after[i].err) }));
const stat = (k) => { const a = rows.map((r) => r[k]).sort((x, y) => x - y); return { max: a[a.length - 1], p50: a[a.length >> 1], mean: +(a.reduce((s, v) => s + v, 0) / a.length).toFixed(2) }; };
console.log(JSON.stringify({ landmarks: rows.length, before: stat("beforeMm"), after: stat("afterMm"), tolMet: R.tolMet, crease: R.crease, attempts: R.attempts, rows }, null, 1));
