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
- Keep the engine free of third-party dependencies. Only the C++ standard library
  and the engine's own headers are included.
- If a change alters rendered output, say so in the pull request and include the
  new `certificate.json` for the default view.
- Do not commit secrets, `.env` files or generated build output.
