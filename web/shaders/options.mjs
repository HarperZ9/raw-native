// Option libraries for the physical shaders, each option a data record with provenance.
//
// OPTION_DATA (options-data.mjs, generated from the measured shader-option table) holds CRT masks,
// phosphor variants, tube classes and display modes, and lens coatings. The two large tables register
// themselves when their module loads, so a page that only uses the CRT does not download them:
// lens/glasses.mjs adds the catalogue glasses (SCHOTT, OHARA, HOYA, CDGM, SUMITA, HIKARI) and sensor.mjs
// the camera spectral sensitivities.
// Every record says where its numbers came from: prov.status is measured, derived, fitted, defined,
// secondary or from_memory (which must carry a confidence word). option() refuses a record without it,
// so an unsourced number cannot enter a look through this door.
import { OPTION_DATA } from "./options-data.mjs";

export { OPTION_DATA };
export const STATUSES = new Set(["measured", "derived", "fitted", "defined", "secondary", "from_memory", "missing"]);

export class ProvenanceMissing extends Error {}

export function requireProv(rec, inherited) {
  const p = (rec && rec.prov) || inherited;
  const id = rec && rec.id;
  if (!p || typeof p !== "object") throw new ProvenanceMissing(`option ${id}: no provenance`);
  if (!STATUSES.has(p.status)) throw new ProvenanceMissing(`option ${id}: status ${p.status} is not one of ${[...STATUSES].join(", ")}`);
  if (!p.citation && !p.source) throw new ProvenanceMissing(`option ${id}: no citation or source`);
  if (p.status === "from_memory" && !["high", "moderate", "low"].includes(p.confidence)) throw new ProvenanceMissing(`option ${id}: from_memory without a confidence word`);
  return p;
}

const GROUPS = {
  "crt.mask": () => [OPTION_DATA.crt.masks, null],
  "crt.phosphor": () => [OPTION_DATA.crt.phosphors, null],
  "crt.tube": () => [OPTION_DATA.crt.tubes, null],
  "crt.mode": () => [OPTION_DATA.crt.modes, null],
  "crt.maskRecord": () => [OPTION_DATA.crt.maskRecords, null],
  "lens.coating": () => [OPTION_DATA.lens.coatings, null],
};
export const GROUP_NAMES = Object.keys(GROUPS);
// A table module adds its group: rows and the provenance its rows inherit.
export function registerOptionGroup(name, rows, prov) {
  requireProv({ id: name, prov });
  GROUPS[name] = () => [rows, prov];
  if (!GROUP_NAMES.includes(name)) GROUP_NAMES.push(name);
}

// One record by group and id, checked; or a caller's own record (an object) checked the same way.
export function option(group, idOrRecord) {
  if (typeof idOrRecord === "object") { requireProv(idOrRecord); return idOrRecord; }
  if (!GROUPS[group]) throw new Error(`option group ${group} is not loaded`);
  const [rows, inherited] = GROUPS[group]();
  const rec = rows.find((r) => r.id === idOrRecord);
  if (!rec) throw new Error(`no ${group} option ${idOrRecord}`);
  return { ...rec, prov: requireProv(rec, inherited) };
}
export function options(group) {
  const [rows, inherited] = GROUPS[group]();
  return rows.map((r) => ({ ...r, prov: requireProv(r, inherited) }));
}
