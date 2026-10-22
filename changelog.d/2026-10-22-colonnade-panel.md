- **Colonnade live panel** (`web/colonnade/`). A page with sliders for the sun, the haze, the
  exposure and the samples that re-renders the path-traced colonnade through the engine's own
  WebAssembly build: a quick 2-sample pass, then a finer one. CI drives it in headless Chrome
  and checks that each setting reaches the engine and changes the frame.
