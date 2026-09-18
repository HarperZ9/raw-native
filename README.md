# raw-native

A zero-dependency, two-way rendering engine in C++23.

raw-native renders a small 3D scene two ways and then checks one against the
other. It rasterizes a G-buffer, computes ambient occlusion both as
ray-traced ground truth and as a cheaper screen-space approximation, and runs
a reconcile step that measures how far the approximation drifts from the
truth. The result is written out as a witnessed certificate in JSON. All of
this runs inside a bounded, fail-closed arena allocator that refuses any
allocation past its budget rather than growing.

The engine is original work, standalone, and portable. It has no third-party
dependencies and no platform bindings. The only includes are the C++ standard
library and the engine's own headers, so it builds the same on any toolchain
with a C++23 compiler. There is no graphics API, no GPU, and no operating
system surface involved: every pixel is produced on the CPU.

## Current status

Alpha standalone renderer. The public surface is the CMake library target,
the `raw_native_cli` command-line driver, and the CTest suite. Build from this
checkout; there is no package registry or installer path documented here.

## What is inside

- G-buffer rasterizer: depth, normal, position, albedo, and a coverage mask.
- Ray-traced ambient occlusion: hemisphere sampling against a linear
  acceleration structure, treated as the ground truth.
- Screen-space ambient occlusion: the cheaper approximation computed from the
  G-buffer alone.
- Reconcile step: compares the screen-space approximation against the
  ray-traced truth, reporting per-pixel error, RMSE, max error, and a
  within-tolerance verdict.
- Witnessed certificate: a re-checkable JSON record with a claim, a verdict
  (verified, refuted, or unverifiable), the oracle, and ordered evidence.
- Bounded arena allocator: a bump allocator over caller-provided memory that
  is gated by a budget. Over-budget or zero-size requests are refused, never
  served by growing. It records a lifetime witness (budget, used, high water,
  allocations, refusals) that is itself emitted as a certificate.

## Layout

```
raw/      internal headers (vectors, matrices, image buffers, scene,
          gbuffer, rasterizer, ray AO, SSAO, reconcile, certificate,
          composite, acceleration, arena)
src/      implementation sources
app/      command-line driver (main.cpp)
tests/    unit tests (one executable per test_*.cpp)
```

## Build

Requires CMake 3.24 or newer and a C++23 compiler.

```sh
cmake -B build -S .
cmake --build build
```

This creates the `raw_native` static library and, when `app/main.cpp` is
present, the `raw_native_cli` executable.

## Test

```sh
ctest --test-dir build --output-on-failure
```

For multi-config generators, select the configuration that was built:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

## Run

The command-line driver renders a 256x256 test scene and writes its outputs to
a directory you choose (defaults to the current directory):

```sh
mkdir -p out
./build/raw_native_cli ./out
```

On multi-config generators, the executable may be under the configuration
directory:

```powershell
New-Item -ItemType Directory -Force out | Out-Null
.\build\Debug\raw_native_cli.exe .\out
```

It writes the shaded frame (`frame.ppm`), the two ambient-occlusion buffers
(`ao_rt.pgm`, `ao_ss.pgm`), the reconcile error map (`ao_error.pgm`), and the
witnessed certificates (`certificate.json`, `arena_certificate.json`). The
driver runs two passes: a measure pass that records the exact memory footprint,
then a render pass bounded to exactly that footprint, so an over-budget render
fails closed and emits a breach certificate instead of growing.

## Configuration

The demo driver has one runtime argument: the output directory. Scene size,
sampling count, reconciliation tolerance, and memory-budget behavior are
compiled into `app/main.cpp` for the current demo. Library users can call the
headers under `raw/` and implementations under `src/` directly.

No credentials, network services, GPU drivers, browser profiles, or private
state are required.

## Troubleshooting

- `cmake` cannot find a compiler: install or select a C++23-capable toolchain.
- `ctest` reports no tests: rerun `cmake -B build -S .` and check that
  `tests/test_*.cpp` files were discovered.
- `raw_native_cli` is not at `./build/raw_native_cli`: check whether your
  generator wrote it under `build/Debug/` or another configuration directory.
- The output directory does not exist: create it before running the driver.
- The driver prints an arena breach: inspect `arena_certificate.json`; the
  renderer failed closed instead of growing past the budget.

## Limitations

raw-native renders a fixed CPU test scene and emits local certificate files. It
does not render arbitrary assets, open windows, use a GPU, dispatch native UI
actions, or certify hardware color/display behavior. The certificates describe
the demo's computed render and allocator witness only.

## License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 Zain Dana Harper.
