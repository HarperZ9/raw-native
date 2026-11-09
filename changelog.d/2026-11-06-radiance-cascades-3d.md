- **Radiance cascades in 3D** (`web/shaders/cascades3d`, `docs/radiance-cascades-3d.md`). World-space
  cascades for diffuse GI on triangle scenes, CPU (float64) and WebGPU, with screen probes as a
  lower-density cascade 0 and a per-probe-pair visibility fix. On raw-hall nave against the 8-bounce
  path trace at 1080p on an RTX 4090: relative RMSE 0.104 (no-GI baseline 0.415) in 3.6 to 4.0 ms of
  GI passes; ROADMAP P6's targets were 0.30 and 6 ms. The thin-wall leak scene keeps a closed room
  under 0.1% of the lit room. `raw_native_cli gi-reference` exports scenes and renders the references.
