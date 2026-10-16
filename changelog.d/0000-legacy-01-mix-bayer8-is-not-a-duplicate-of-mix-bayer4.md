- **`mix-bayer8` is not a duplicate of `mix-bayer4`.** Shader lab 4 reported it identical by
  construction. That holds at strength 1 only; below it the modes differ, so `mix-bayer8` keeps its
  behaviour and is not made an alias. `docs/shaders/README.md` said "the same indices" without the
  condition and is corrected; `web/shaders/dither-mix.test.mjs` checks both cases on four palettes.
