// Catalogue glasses and coatings for the lens suite, from the option library (options.mjs).
//
// A catalogue glass carries its maker's dispersion formula as compiled in the refractiveindex.info
// release (dedicated to the public domain): formula 1 and 2 are Sellmeier forms, formula 3 the polynomial (SCHOTT's old form).
// glassIndex in spectrum.mjs evaluates any of them, so a catalogue glass can stand wherever GLASS.crown
// or GLASS.flint does.
import { option, registerOptionGroup } from "../options.mjs";
import { GLASS_DATA } from "./glass-data.mjs";

registerOptionGroup("lens.glass", GLASS_DATA.rows, GLASS_DATA.prov);
export { GLASS_DATA };
import { riiIndex } from "./spectrum.mjs";

export { riiIndex };

// "maker:glass", for example "schott:N-BK7", "ohara:S-FPL51", "hoya:FCD1", "cdgm:H-K9L".
export function catalogGlass(id) {
  const r = option("lens.glass", id);
  return { name: r.id, formula: r.formula, c: r.c, range_um: r.range_um, nd: r.nd, vd: r.vd, prov: r.prov };
}

// The coating parameters coating.mjs reads ({ n1, quarterAt, ns }) from a coating option. An uncoated
// surface is a layer of index 1 (air), which reflects as the bare glass.
export function coatingParams(id, ns = 1.52) {
  const r = option("lens.coating", id);
  if (!r.layers.length) return { n1: 1.0, quarterAt: r.design_nm || 550, ns };
  if (r.layers.length > 1) throw new Error(`${id}: coating.mjs models one layer; ${r.layers.length}-layer designs are library data only`);
  return { n1: r.layers[0].n, quarterAt: r.design_nm, ns };
}

// Eras of lens build: which glass and coating a vintage, a coated mid-century and a modern lens use.
// These are design choices over measured glass data (status defined), not claims about one product.
export const LENS_ERAS = {
  vintage: { glass: { crown: "schott:N-K5", flint: "schott:N-F2" }, coating: "uncoated",
    prov: { status: "defined", citation: "uncoated classic crown and flint (SCHOTT N-K5 and N-F2 formulas, refractiveindex.info); the pre-1940 build" } },
  coated: { glass: { crown: "schott:N-BK7", flint: "schott:N-SF2" }, coating: "mgf2-quarter-wave",
    prov: { status: "defined", citation: "single MgF2 quarter-wave on the suite's own N-BK7 / N-SF2; the 1950s to 1970s build" } },
  modern: { glass: { crown: "ohara:S-FPL51", flint: "ohara:S-NBH5" }, coating: "mgf2-quarter-wave",
    prov: { status: "defined", citation: "fluorophosphate low-dispersion crown with a modern flint (OHARA formulas, refractiveindex.info); the multi-coat stays library data" } },
};

// Lens parameters for an era: the coating and the crown glass whose dispersion sets the lateral colour.
// The ghost prescription keeps its own design glasses.
export function lensEra(name) {
  const e = LENS_ERAS[name];
  if (!e) throw new Error(`unknown lens era ${name}`);
  const crownGlass = catalogGlass(e.glass.crown);
  return { coating: coatingParams(e.coating, crownGlass.nd), crownGlass, era: name, prov: e.prov };
}
