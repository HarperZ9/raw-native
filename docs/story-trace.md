# Story trace format (lw-trace/1)

A trace is a record of one run of a story, written as JSON lines in a fixed form so two
implementations of the story VM can be compared byte for byte. The JS harness in Low Water
writes it; `raw/story` writes it from C++; `lw_trace_replay` replays a recorded run and checks the
two are identical.

Each line is one object with no spaces, keys in the order shown, strings escaped as
`JSON.stringify` escapes them, numbers in the shortest form that reads back to the same double
(`1790`, `0.1`, `1e-7`). The `begin` record carries `"v":1`. A reader that meets an unknown `t` must
pass the record through, so new record types (combat events, for example) extend the format without
breaking old traces. Bump `v` only when an existing record changes meaning.

## Records the VM owns (replayed and compared)

| t | fields | when |
|---|---|---|
| begin | v, seed, policy, clock, place, vars, flags, start, exits | run start (an input) |
| talk | node, clock | a conversation opens (input) |
| take | i, key | an option is chosen (input) |
| after | k | a fight, run or sneak ends (input) |
| next | | the harness asks for the next scene (input) |
| node | id, clock | a node is entered |
| line | k, who, text, clock, node | a line is shown (k: nar, line, inst, voice, you, check) |
| offer | node, opts[{key, kind, locked}] | a menu is shown |
| var | n, v | a variable is assigned |
| flag | id, on | a flag is set or cleared |
| time | from, to, cut? | the clock moves |
| place | id | the place changes |
| end | id | the story ends |
| scene | kind, id, node | a meter scene or event is due |
| stop | id | the recording stopped at the first node outside a prefix |

## Records the host owns

`h` is a host answer: `{"t":"h","f":name,"a":args,"r":result}`. The VM makes the call, the host
answers. The replayer feeds answers from the recorded trace in order, and a call whose name or
arguments differ from the next recorded one is a divergence. `rng` is a draw
`{"seed","key","v"}`; the VM recomputes `v` with the same hash. `ev` is a host event passed
through without checking. Any state record with `"s":"h"` was made by the host and is applied to the VM.

Host call names: `get`, `assign`, `call:<name>`, `checks.passive`, `checks.preview`,
`checks.resolve`, `sheet.skill`, `sheet.addXp`, `pressure.takeFull`, `pressure.afterFull`, `fill`,
`command`, `read`, `combat.take`, `combat.finish`, `runs.take`, `runs.finish`, `sneaks.take`, `sneaks.finish`.
For `call:<name>` the argument is the list of argument names or literals; an expression argument is
written `"<expr>"` because the host evaluates it.

## Deterministic arithmetic in the VM

Only these operations affect a trace, and each is exact or defined bit for bit on both sides:

- `hashStr` uses 32-bit integer xor and `Math.imul` (wrapping multiply) over UTF-16 code units.
  `rand01` is that hash divided by 4294967296, an exact power-of-two division.
- The clock is a double. It changes by addition only (`cost`, `wait`, `cut`, host time records).
- Expressions use `+ - * /` on doubles, `%` as `fmod` (zero divisor gives 0), `/` with a zero
  divisor gives 0, and `Math.min`, `Math.max`, `Math.abs`. No `Math.floor`, `sin`, `pow` or any
  transcendental function is used by the VM or the trace.
- Number text: shortest round-trip form. `-0` is written `0`, non-finite values are written `null`.

## Not ported yet

Interrogations (`^interrog`), `liveLine` character lines, and everything behind the host calls
above: rules, community model, world record, combat. See evidence/story-vm-bounds.md.
