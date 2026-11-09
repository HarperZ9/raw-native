- **`tools/asset` decimation checks the whole face, not only its edges.** The quadric collapse now measures the field
  distance at each affected face's centroid and edge midpoints and refuses a collapse that pushes a face past the
  tolerance unless it already was past it. On a head-like test sculpt the max sag at a 6 mm tolerance went from 6.19 mm to
  5.99 mm; `sdfkit.test.mjs` asserts it and fails on the old code.
