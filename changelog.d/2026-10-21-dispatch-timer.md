- **Shading pass timer** (`material-gallery`'s `dispatch_ms`). The GPU shading pass is
  now timed on its own, warm, as the median submit-to-fence time of its dispatches, so
  showcase timings are frame times rather than wall times with setup. The sphere field's
  1080p shading pass takes 3.6 ms on an RTX 4090. Below about a millisecond the figure is
  mostly submission latency and varies between runs.
