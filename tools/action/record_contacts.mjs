// Records contact traces from Low Water's JS combat engine: fights where the swing decides the hit. The C++
// action layer replays them (tests/test_action_contact.cpp, docs/action-schedule.md) and must find the same
// contacts with its own capsule kernel (raw/game/action/contact.hpp) and write the same bytes.
//
//   node tools/action/record_contacts.mjs <low-water>/game/engine/combat <outdir> [runs=40] [ticks=600]
//
// Each run builds the real world (createWorld, W.step, fighter.mjs, hits.mjs findContacts with sim/capsule.mjs,
// applyHit) on the floor arena with barks off: Lamb and one foe 1.5 m apart. A seeded script moves the sticks
// (hold, approach, back off, circle, kept on a leash around the spots) and presses actions; W.step finds every
// contact itself. The game owns positions, so the poses the contacts were found at are inputs (`apos`, on
// change), and so are the game's damage factors of a contact (`afac`: a weak point found, a blow glancing off a
// shield). Outputs: the contacts found (`ahit`), the timeline events with the hit's shake and rumble (`aev`),
// and both fighters' timeline state (`ast`). Every run is recorded twice; a difference fails the run.
// G1's recorder (record_timeline.mjs) scripted its contacts between steps; this one does not script any.
import { mkdirSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [dir, outdir, nArg, tArg] = process.argv.slice(2);
if (!dir || !outdir) { console.error("usage: record_contacts.mjs <combat-dir> <outdir> [runs] [ticks]"); process.exit(2); }
const C = pathToFileURL(resolve(dir)).href + "/";
const { makeG } = await import(C + "tests/helpers.mjs");
const { createWorld } = await import(C + "sim/world.mjs");
const { chooseRoll } = await import(C + "sim/fighter.mjs");
const { lambDef, foeDef, fighterSpec } = await import(C + "roster.mjs");
const { ARCHETYPES } = await import(C + "data/foes.mjs");
const { TOOLS } = await import(C + "data/tools.mjs");
const { FLOOR } = await import(C + "data/arenas.mjs");
const { RULES } = await import(C + "data/common.mjs");
const { FEEL } = await import(C + "sim/hits.mjs");
const { COMPOSURE } = await import(C + "data/build.mjs");

const RUNS = +(nArg || 40), TICKS = +(tArg || 600);
const LAMB_AT = [13.5, 1.0], FOE_AT = [15.0, 1.0];
const STYLES = Object.keys(ARCHETYPES).sort(), TOOL_IDS = Object.keys(TOOLS).sort();
const EVENTS = new Set(["move", "tell", "breathless", "evade", "parry", "hit", "flinch", "stagger", "knockdown", "fall", "gust", "nocomposure", "shake", "rumble"]);
const KEEP = ["action", "amp", "armor", "by", "dir", "dmg", "frames", "high", "id", "kind", "low", "move", "poise", "spring"];
const ACTIONS = ["light", "light", "light", "heavy", "catch", "step", "roll"];
const sortKeys = (v) => (Array.isArray(v) ? v.map(sortKeys) : v && typeof v === "object" ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortKeys(v[k])])) : v);
const sha = (s) => createHash("sha256").update(s).digest("hex");

function mulberry(seed) { let a = seed >>> 0; return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// A contact the C++ layer ports: a plain strike (no seize, shove, snare, takedown, drain or drag).
const strike = (m) => (m.kind === "attack" || m.kind === "riposte") && !m.grapple && !m.shove && !m.snare && !m.through && !m.takedown && !m.drain && !m.toward;
// A special a foe may press by id: a strike that is no item, call, summon, wall charge or art.
const pressable = (m) => strike(m) && (m.dmg > 0 || m.poise > 0) && !m.item && !m.shout && !m.summon && !m.wallStun && !m.composure && !m.charge;

function setup(i) {
  const tool = TOOL_IDS[i % TOOL_IDS.length], style = STYLES[(i * 3) % STYLES.length];
  const learned = i % 3 === 0 ? ["feint"] : [], ballast = i % 5 === 0 ? 6 : i % 5 === 1 ? 5 : 3;
  // Lamb's run strike needs a held run button (never pressed here); no riposte or takedown is in the moveset
  const lamb = fighterSpec("lamb", "lamb", lambDef({ ballast }, tool, { hp: 1000 }, learned), LAMB_AT, { face: 0 });
  const foe = fighterSpec("foe0", "foe", foeDef(null, style, { hp: 1000 }), FOE_AT, { face: -Math.PI });
  const W = createWorld({ G: makeG(), arena: FLOOR, fighters: [lamb, foe], seed: i + 1, barks: false });
  return { W, tool, style, learned, ballast };
}

function moveset(W, F) {
  const set = W.defs[F.id].moveset, out = {};
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
    moveset: moveset(W, F), owns: d.moves.filter((id) => pressable(W.moves[id])).sort(),
    radius: d.radius, height: d.height, x: F.x, z: F.z, face: F.face, nose8: !!(d.breaks || {}).nose8,
  };
}

// What the timeline reads of the ground (poise scale, regen, the roll's form); the ground's height may vary.
const footKey = (W, F) => { const f = W.field.footing(F.x, F.z); return JSON.stringify([f.surface, f.kind, f.band, f.regen, f.recovery, !!f.frost, !!f.wet]); };
const stateOf = (F) => [F.id, F.act ? F.act.id : null, F.act ? F.act.f : 0, F.act ? F.act.end : 0, F.act ? F.act.charge : 0, F.act && F.act.charging ? 1 : 0, F.hitstop, F.buf.length, F.st, F.poise, F.hp, F.composure];
const evRecord = (tick, e) => { const d = {}; for (const k of KEEP) if (e[k] !== undefined) d[k] = sortKeys(e[k]); return JSON.stringify({ t: "aev", tick, n: e.type, d }); };

// The stick: a mode held for a while (still, approach, back off, circle left or right), turned to approach or back
// off when the two drift past the leash, and back toward the spot when a fighter strays.
function stick(rnd, st, F, O, spot) {
  if (st.left <= 0) { st.mode = Math.floor(rnd() * 5); st.left = 20 + Math.floor(rnd() * 60); } else st.left--;
  const dx = O.x - F.x, dz = O.z - F.z, d = Math.hypot(dx, dz) || 1, ux = dx / d, uz = dz / d;
  let m = st.mode;
  if (d > 2.6) m = 1; else if (d < 0.9) m = 2;
  if (Math.hypot(F.x - spot[0], F.z - spot[1]) > 1.2) return { mx: (spot[0] - F.x) * 0.5, mz: (spot[1] - F.z) * 0.5 };
  const k = 0.6;
  return m === 1 ? { mx: ux * k, mz: uz * k } : m === 2 ? { mx: -ux * k, mz: -uz * k } : m === 3 ? { mx: -uz * k, mz: ux * k } : m === 4 ? { mx: uz * k, mz: -ux * k } : { mx: 0, mz: 0 };
}

function record(i) {
  const { W, tool, style, learned, ballast } = setup(i), rnd = mulberry(5000 + i), out = [];
  const [L, Fo] = W.fighters, table = JSON.stringify(sortKeys(W.moves)), tname = sha(table).slice(0, 12);
  const config = { buffer: RULES.buffer, catchLockout: RULES.catchLockout, flinchGuard: RULES.flinchGuard, poiseDelay: RULES.poiseDelay, poiseRegen: RULES.poiseRegen,
    knockdownPoise: RULES.knockdownPoise, hitstopLight: FEEL.hitstop.light, hitstopHeavy: FEEL.hitstop.heavy, heavyFrom: FEEL.heavyFrom, crowdAt: FEEL.crowdAt, crowdCap: FEEL.crowdCap,
    parriedHold: 12, composureCatch: COMPOSURE.catch, composureEvade: COMPOSURE.evade, composureHit: COMPOSURE.hit, composureRiposte: COMPOSURE.riposte, parryCone: RULES.parryCone, fx: true };
  out.push(JSON.stringify({ t: "abegin", v: 1, kind: "contact", seed: i + 1, tool, style, learned, ballast, table: tname, config, actors: [actorHead(W, L), actorHead(W, Fo)] }));
  const holdUntil = { lamb: 0, foe0: 0 }, lastHold = { lamb: false, foe0: false }, sticks = { lamb: { left: 0 }, foe0: { left: 0 } };
  const spots = { lamb: LAMB_AT, foe0: FOE_AT }, foot0 = footKey(W, L);
  let lastPose = JSON.stringify(W.fighters.map((F) => [F.id, F.x, F.z, F.face])), counts = { contacts: 0, hits: 0 };
  for (let k = 1; k <= TICKS; k++) {
    const inputs = {}, n0 = out.length;
    for (const F of W.fighters) {
      const press = [], O = F === L ? Fo : L;
      if (rnd() < 0.08) {
        const own = F.team === "lamb" ? [] : actorHead(W, F).owns, list = [...ACTIONS, ...own], a = list[Math.floor(rnd() * list.length)];
        press.push(a); if (a === "heavy") holdUntil[F.id] = k + Math.floor(rnd() * 61);
      }
      const hold = k < holdUntil[F.id];
      inputs[F.id] = { ...stick(rnd, sticks[F.id], F, O, spots[F.id]), press, hold: { heavy: hold } };
      if (press.length || hold !== lastHold[F.id]) out.push(JSON.stringify({ t: "ain", tick: k, id: F.id, press, hold }));
      lastHold[F.id] = hold;
    }
    const evs = W.step(inputs);
    if (W.result) throw new Error(`run ${i}: the world ended at ${k}: ${JSON.stringify(W.result)}`);
    // a knockback can carry a body off the floor's stone: the game sets it back on its leash (positions are the game's)
    for (const F of W.fighters) { const s = spots[F.id], dx = F.x - s[0], dz = F.z - s[1], d = Math.hypot(dx, dz); if (d > 1.5) { F.x = s[0] + (dx / d) * 1.5; F.z = s[1] + (dz / d) * 1.5; F.vx = F.vz = F.kx = F.kz = 0; } }
    for (const F of W.fighters) if (footKey(W, F) !== foot0) throw new Error(`run ${i}: ${F.id} footing changed at ${k}`);
    const pose = JSON.stringify(W.contactPoses);
    const nPos = out.length;   // a pose line alone does not call for a state line
    if (pose !== lastPose) { out.push(JSON.stringify({ t: "apos", tick: k, p: W.contactPoses })); lastPose = pose; }
    const posed = out.length - nPos;
    for (const [a, t, mid] of W.contacts) {
      if (!strike(W.moves[mid])) throw new Error(`run ${i}: a contact by ${mid} at ${k} is not a plain strike`);
      out.push(JSON.stringify({ t: "ahit", tick: k, by: a, to: t, move: mid }));
      counts.contacts++;
      const saw = (type) => evs.some((e) => e.type === type && e.id === t && e.by === a);
      if (saw("sneakhit") || saw("takedown")) throw new Error(`run ${i}: a sneak hit at ${k}`);
      const weak = saw("weakpoint") ? 1 + W.defs[a].bonus.weak : 1, glance = saw("glance") ? 1 - W.defs[t].frontGuard : 1;
      if (weak !== 1 || glance !== 1) out.push(JSON.stringify({ t: "afac", tick: k, by: a, to: t, weak, glance }));
    }
    for (const e of evs) if (EVENTS.has(e.type)) { out.push(evRecord(k, e)); if (e.type === "hit") counts.hits++; }
    // the state line: on every tick that wrote an input, contact or event line, and on every fourth tick
    if (out.length - posed > n0 || k % 4 === 0) out.push(JSON.stringify({ t: "ast", tick: k, a: W.fighters.map(stateOf) }));
  }
  out.push(JSON.stringify({ t: "aend", tick: TICKS }));
  return { text: out.join("\n") + "\n", table, tname, counts, name: `${String(i + 1).padStart(2, "0")}-${tool}-${style}.jsonl` };
}

mkdirSync(outdir, { recursive: true });
const NL = String.fromCharCode(10);
const manifest = {}, pool = {}, tables = {}, total = { contacts: 0, hits: 0 };
for (let i = 0; i < RUNS; i++) {
  const a = record(i), b = record(i);
  if (a.text !== b.text) { console.error(`run ${i + 1}: two recordings differ`); process.exit(1); }
  writeFileSync(join(outdir, a.name), a.text);
  const ids = {};
  for (const [id, m] of Object.entries(JSON.parse(a.table))) { const j = JSON.stringify(m), h = sha(j).slice(0, 16); pool[h] = m; ids[id] = h; }
  tables[a.tname] = ids;
  manifest[a.name] = { sha256: sha(a.text), lines: a.text.split(NL).length - 1, table: a.tname, contacts: a.counts.contacts, hits: a.counts.hits };
  total.contacts += a.counts.contacts; total.hits += a.counts.hits;
}
writeFileSync(join(outdir, "tables.json"), JSON.stringify(sortKeys({ pool, tables })) + NL);
writeFileSync(join(outdir, "manifest.json"), JSON.stringify(manifest, null, 1) + NL);
console.log(`${RUNS} runs of ${TICKS} ticks, each recorded twice with identical bytes; ${total.contacts} contacts found by capsules, ${total.hits} hits; ${Object.keys(pool).length} distinct move records`);
