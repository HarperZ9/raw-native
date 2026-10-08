# raw-native explainer

`index.html` is a single self-contained page that explains how raw-native
checks its screen-space ambient occlusion against a ray-traced reference. It is
published at <https://harperz9.github.io/repo-explainers/raw-native.html>.

Open `index.html` in a browser to read it from a checkout. The four renders it
shows load from `docs/images/` at commit f2cd6e9, and the "Run it in this page"
panel loads the 0.4.0 WebAssembly build from harperz9.github.io after checking
both files against their SHA-256.

The certificate values, verify outcomes and memory numbers on the page come from
`raw_native_cli` built from commit f2cd6e9. The two cross-section drawings are a
2D slice that applies the rules of `src/renderer/ray_ao.cpp` and
`src/renderer/ssao.cpp`; their counts describe the drawing, not a render. When
the renderer changes, rerun the commands in the page's "Try it" section and
update the values in the same pull request.
