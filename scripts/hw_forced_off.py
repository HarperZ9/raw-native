#!/usr/bin/env python3
"""HW H1.0 forced-off check (evidence/hw-h1-0-bounds.json, check "forced_off"), stdlib only.

Runs gpu_hw_probe with RAW_NATIVE_HW_DISABLE unset, set to "all", and set to each feature
name alone, then checks:
  - all: no feature reported supported; every functional check that dispatched ran its
    fallback (or created no pipeline) and matched the CPU;
  - one name: exactly that feature's "supported" changes against the unset run, and the
    functional checks still pass.
Usage: hw_forced_off.py PATH/TO/gpu_hw_probe[.exe] OUT.json [ARGS...]
  ARGS are passed to the executable (raw_native_vk_probe needs --checks).
Exit 0 when every case meets the bound, 1 otherwise.
"""
import json
import os
import subprocess
import sys


def run(exe, disable, args=()):
    env = dict(os.environ)
    env.pop("RAW_NATIVE_HW_DISABLE", None)
    if disable is not None:
        env["RAW_NATIVE_HW_DISABLE"] = disable
    out = subprocess.run([exe, *args], env=env, capture_output=True, text=True, check=False).stdout
    return json.loads(out)


def supported(rep):
    return {f["name"]: f["supported"] for f in rep["probe"]["features"]}


def functional_ok(rep):
    return all(c["pass"] for c in rep["functional"]) and len(rep["functional"]) > 0


def main():
    exe, out, args = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3:]
    base = run(exe, None, args)
    base_sup = supported(base)
    cases = []
    allrep = run(exe, "all", args)
    all_sup = supported(allrep)
    fallbacks = all(c["ran_fallback"] or not c["dispatched"] for c in allrep["functional"])
    cases.append({"disable": "all", "supported_after": sorted(k for k, v in all_sup.items() if v),
                  "every_dispatch_used_fallback": fallbacks, "functional_pass": functional_ok(allrep),
                  "pass": not any(all_sup.values()) and fallbacks and functional_ok(allrep)})
    for name in sorted(base_sup):
        rep = run(exe, name, args)
        sup = supported(rep)
        changed = sorted(k for k in base_sup if base_sup[k] != sup.get(k))
        want = [name] if base_sup[name] else []
        cases.append({"disable": name, "changed": changed, "expected_change": want,
                      "functional_pass": functional_ok(rep), "pass": changed == want and functional_ok(rep)})
    ok = all(c["pass"] for c in cases)
    rec = {"schema": "raw-native.evidence/1", "bounds": "evidence/hw-h1-0-bounds.json", "check": "forced_off",
           "adapter": base["probe"]["adapter"], "backend": base["probe"]["backend"], "cases": cases, "pass": ok}
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(rec, f, indent=1)
        f.write("\n")
    for c in cases:
        print(("PASS " if c["pass"] else "FAIL ") + json.dumps(c))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
