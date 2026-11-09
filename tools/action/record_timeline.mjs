// Records action-timeline traces from Low Water's JS combat engine, for the C++ parity test of
// raw/game/action (tests/test_action_trace.cpp, docs/action-timeline.md).
//
//   node tools/action/record_timeline.mjs <low-water>/game/engine/combat <outdir> [runs=40] [ticks=480]
//
// Each run builds the real world (createWorld, W.step, fighter.mjs, hits.mjs applyHit) on the
// floor arena with barks off: Lamb and one foe 8 m apart, facing each other, set back on their
// spots after every step (position is not part of the timeline). A seeded script presses actions,
// holds heavy for a charge, and lands contacts through applyHit between steps. The trace holds the
// inputs, the contacts, the timeline events and both fighters' timeline state (on every tick that
// wrote another line, and every fourth tick).
// Every run is recorded twice; a difference between the two runs is reported and fails the run.
import { mkdirSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [dir, outdir, nArg, tArg] = process.argv.slice(2);
if (!dir || !outdir) { console.error("usage: record_timeline.mjs <combat-dir> <outdir> [runs] [ticks]"); process.exit(2); }
const C = pathToFileURL(resolve(dir)).href + "/";
const { makeG } = await import(C + "tests/helpers.mjs");
const { createWorld } = await import(C + "sim/world.mjs");
const { applyHit } = await import(C + "sim/hits.mjs");
const { chooseRoll } = await import(C + "sim/fighter.mjs");
const { lambDef, foeDef, fighterSpec } = await import(C + "roster.mjs");
const { ARCHETYPES } = await import(C + "data/foes.mjs");
const { TOOLS } = await import(C + "data/tools.mjs");
const { FLOOR } = await import(C + "data/arenas.mjs");
const { RULES } = await import(C + "data/common.mjs");
const { FEEL } = await import(C + "sim/hits.mjs");
const { COMPOSURE } = await import(C + "data/build.mjs");

const RUNS = +(nArg || 40), TICKS = +(tArg || 480);
const LAMB_AT = [13.5, 2.5], FOE_AT = [21.5, 2.5];
const STYLES = Object.keys(ARCHETYPES).sort(), TOOL_IDS = Object.keys(TOOLS).sort();
const EVENTS = new Set(["move", "tell", "breathless", "evade", "parry", "hit", "flinch", "stagger", "knockdown", "fall", "gust", "nocomposure"]);
const KEEP = ["action", "armor", "by", "dmg", "frames", "id", "kind", "move", "poise"];
const ACTIONS = ["light", "light", "light", "heavy", "catch", "step", "roll"];
const sortKeys = (v) => (Array.isArray(v) ? v.map(sortKeys) : v && typeof v === "object" ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortKeys(v[k])])) : v);
const sha = (s) => createHash("sha256").update(s).digest("hex");

function mulberry(seed) { let a = seed >>> 0; return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// A move a scripted contact may carry: a plain strike (no seize, shove, snare or takedown).
const strike = (m) => (m.kind === "attack" || m.kind === "riposte") && (m.dmg > 0 || m.poise > 0) && !m.grapple && !m.shove && !m.snare && !m.through && !m.takedown && !m.drain && !m.toward;
// A special a foe may press by id: no item, call, summon, wall charge or seize.
const pressable = (m) => strike(m) && !m.item && !m.shout && !m.summon && !m.wallStun && !m.composure && !m.charge;

function setup(i) {
  const tool = TOOL_IDS[i % TOOL_IDS.length], style = STYLES[(i * 3) % STYLES.length];
  const learned = i % 3 === 0 ? ["feint"] : [], ballast = i % 5 === 0 ? 6 : i % 5 === 1 ? 5 : 3;
  const lamb = fighterSpec("lamb", "lamb", lambDef({ ballast }, tool, { hp: 1000 }, learned), LAMB_AT, { face: 0 });
  const foe = fighterSpec("foe0", "foe", foeDef(null, style, { hp: 1000 }), FOE_AT, { face: -Math.PI });
  const W = createWorld({ G: makeG(), arena: FLOOR, fighters: [lamb, foe], seed: i + 1, barks: false });
  return { W, tool, style, learned, ballast };
}

// What each action name means for this fighter on this footing (fighter.mjs resolveAction, for a
// fighter that is not running, has no riposte or takedown target and no learned variant).
function moveset(W, F) {
  const def = W.defs[F.id], set = def.moveset, out = {};
  for (const a of ["light", "heavy", "catch", "step"]) if (typeof set[a] === "string") out[a] = set[a];
  out.roll = chooseRoll(W, F, set.roll);
  return out;
}

function actorHead(W, F) {
  const d = W.defs[F.id], lv = d.levels || {};
  return {
    id: F.id, team: F.team, hp: F.hp, stamina: F.stMax, poise: F.poiseMax, staminaRegen: d.staminaRegen, staminaDelay: d.staminaDelay,
    stun: d.tempo ? d.tempo.stun : 1, soften: d.soften || 1, power: d.dmgMul || 1, composure: F.composure, composureMax: F.composureMax,
    regain: d.tempo ? d.tempo.regain || 1 : 1, steadyLight: (lv.ballast ?? 0) >= 5, keepFeet: (lv.ballast ?? 0) >= 6,
    footRegen: W.field.footing(F.x, F.z).regen, footRecovery: W.field.footing(F.x, F.z).recovery,
    moveset: moveset(W, F), owns: W.defs[F.id].moves.filter((id) => pressable(W.moves[id])).sort(),
  };
}

const stateOf = (F) => [F.id, F.act ? F.act.id : null, F.act ? F.act.f : 0, F.act ? F.act.end : 0, F.act ? F.act.charge : 0, F.act && F.act.charging ? 1 : 0, F.hitstop, F.buf.length, F.st, F.poise, F.hp, F.composure];
const evRecord = (tick, e) => { const d = {}; for (const k of KEEP) if (e[k] !== undefined) d[k] = e[k]; return JSON.stringify({ t: "aev", tick, n: e.type, d }); };

function record(i) {
  const { W, tool, style, learned, ballast } = setup(i), rnd = mulberry(1000 + i), out = [];
  const [L, Fo] = W.fighters, table = JSON.stringify(sortKeys(W.moves)), tname = sha(table).slice(0, 12);
  const config = { buffer: RULES.buffer, catchLockout: RULES.catchLockout, flinchGuard: RULES.flinchGuard, poiseDelay: RULES.poiseDelay, poiseRegen: RULES.poiseRegen,
    knockdownPoise: RULES.knockdownPoise, hitstopLight: FEEL.hitstop.light, hitstopHeavy: FEEL.hitstop.heavy, heavyFrom: FEEL.heavyFrom, crowdAt: FEEL.crowdAt, crowdCap: FEEL.crowdCap,
    parriedHold: 12, composureCatch: COMPOSURE.catch, composureEvade: COMPOSURE.evade };
  out.push(JSON.stringify({ t: "abegin", v: 1, seed: i + 1, tool, style, learned, ballast, table: tname, config, actors: [actorHead(W, L), actorHead(W, Fo)] }));
  const holdUntil = { lamb: 0, foe0: 0 }, lastHold = { lamb: false, foe0: false };
  const spots = W.fighters.map((F) => [F.x, F.z, F.face]), foot0 = JSON.stringify(W.field.footing(L.x, L.z));
  for (let k = 1; k <= TICKS; k++) {
    const inputs = {}, n0 = out.length;
    for (const F of W.fighters) {
      const press = [];
      if (rnd() < 0.08) {
        const own = F.team === "lamb" ? [] : actorHead(W, F).owns, list = [...ACTIONS, ...own], a = list[Math.floor(rnd() * list.length)];
        press.push(a); if (a === "heavy") holdUntil[F.id] = k + Math.floor(rnd() * 61);
      }
      const hold = k < holdUntil[F.id];
      inputs[F.id] = { mx: 0, mz: 0, press, hold: { heavy: hold } };
      if (press.length || hold !== lastHold[F.id]) out.push(JSON.stringify({ t: "ain", tick: k, id: F.id, press, hold }));
      lastHold[F.id] = hold;
    }
    for (const e of W.step(inputs)) if (EVENTS.has(e.type)) out.push(evRecord(k, e));
    if (W.result) throw new Error(`run ${i}: the world ended at ${k}: ${JSON.stringify(W.result)}`);
    W.fighters.forEach((F, j) => { if (F.face !== spots[j][2]) throw new Error(`run ${i}: ${F.id} turned at ${k}`); F.x = spots[j][0]; F.z = spots[j][1]; F.vx = F.vz = F.kx = F.kz = 0; });
    if (JSON.stringify(W.field.footing(L.x, L.z)) !== foot0) throw new Error(`run ${i}: footing changed at ${k}`);
    if (rnd() < 0.03) {
      const fwd = rnd() < 0.5, A = fwd ? L : Fo, T = fwd ? Fo : L;
      const pool = W.defs[A.id].moves.filter((id) => strike(W.moves[id])).sort(), m = W.moves[pool[Math.floor(rnd() * pool.length)]];
      // the game's damage factors, read back from what applyHit reports: a weak point found in a
      // wind-up (Lamb's PEN read), a blow glancing off a carried shield. A sneak hit is not ported.
      const e0 = W.events.length; applyHit(W, A, T, m);
      const evs = W.events.slice(e0), saw = (t) => evs.some((e) => e.type === t);
      if (saw("sneakhit") || saw("takedown")) throw new Error(`run ${i}: a sneak hit at ${k}`);
      const weak = saw("weakpoint") ? 1 + W.defs[A.id].bonus.weak : 1, glance = saw("glance") ? 1 - W.defs[T.id].frontGuard : 1;
      out.push(JSON.stringify({ t: "ahit", tick: k, by: A.id, to: T.id, move: m.id, frontal: true, crowd: 1, weak, glance }));
      for (const e of evs) if (EVENTS.has(e.type)) out.push(evRecord(k, e));
    }
    // the state line: on every tick that wrote another line, and on every fourth tick
    if (out.length > n0 || k % 4 === 0) out.push(JSON.stringify({ t: "ast", tick: k, a: W.fighters.map(stateOf) }));
  }
  out.push(JSON.stringify({ t: "aend", tick: TICKS }));
  return { text: out.join("\n") + "\n", table, tname, name: `${String(i + 1).padStart(2, "0")}-${tool}-${style}.jsonl` };
}

// The move tables, pooled: each distinct move record once (by the SHA-256 of its JSON), each
// run's table as a map from move id to record. tables.json is what the C++ replay loads.
mkdirSync(outdir, { recursive: true });
const NL = String.fromCharCode(10);
const manifest = {}, pool = {}, tables = {};
for (let i = 0; i < RUNS; i++) {
  const a = record(i), b = record(i);
  if (a.text !== b.text) { console.error(`run ${i + 1}: two recordings differ`); process.exit(1); }
  writeFileSync(join(outdir, a.name), a.text);
  const ids = {};
  for (const [id, m] of Object.entries(JSON.parse(a.table))) { const j = JSON.stringify(m), h = sha(j).slice(0, 16); pool[h] = m; ids[id] = h; }
  tables[a.tname] = ids;
  manifest[a.name] = { sha256: sha(a.text), lines: a.text.split(NL).length - 1, table: a.tname };
}
writeFileSync(join(outdir, "tables.json"), JSON.stringify(sortKeys({ pool, tables })) + NL);
writeFileSync(join(outdir, "manifest.json"), JSON.stringify(manifest, null, 1) + NL);
console.log(`${RUNS} runs of ${TICKS} ticks, each recorded twice with identical bytes; ${Object.keys(pool).length} distinct move records`);
