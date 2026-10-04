# Contributing

Thanks for looking. Keep changes small, tested and easy to check.

Before you open a pull request:

- Build and run the tests:

  ```sh
  cmake -S . -B build
  cmake --build build
  ctest --test-dir build -C Debug --output-on-failure
  ```

- Add or update a test in `tests/` for any change in behavior.
- Keep the engine free of third-party dependencies. Only the C++ standard library,
  the engine's own headers and the vendored superstack header are included.
  The dependency policy and its proposed change are in
  `docs/architecture/adr/0005-dependency-policy.md`.
- Keep includes inside the layer rules: `python scripts/check_layers.py`. A new
  layer or a new edge between layers is an architecture change and needs an ADR
  in `docs/architecture/adr/`.
- A refactor that must not change output proves it: run
  `python scripts/identity_matrix.py run <cli> before.json <dir>` on the build
  before and after, then `identity_matrix.py compare before.json after.json`.
- If a change alters rendered output, say so in the pull request and include the
  new `certificate.json` for the default view.
- Do not commit secrets, `.env` files or generated build output.
