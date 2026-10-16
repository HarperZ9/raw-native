- **Denoiser inference: mean depth in two passes** (HW H2). The single-workgroup reduction took 2.34 ms at 1080p on the
  RTX 4090; two passes take 0.10 ms, and the v4 frame falls from 14.08 to 11.8 ms (prediction committed before: at most
  0.4 ms and 12.2 ms). Timing-only variants of the feature pass show its aux writes cost about 1.0 ms of 2.49. The 2.0 ms
  budget still fails. Parity on the scalar path is unchanged (2.9e-3).
