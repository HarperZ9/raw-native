# Style templates

These are the three starter templates the author chose on 9 October 2026 (ADR 0016). They are M2's style acceptance corpus. Each one shows what the engine does for a kind of game, and each will become a playable starting point. For now they are **scaffolds**: they run, but their art is placeholder art made in code. The art direction is still to come.

| Template | In the author's words | Uses today | Waits on |
|---|---|---|---|
| [painted-iso-rpg](painted-iso-rpg/) | "more Disco Elysium and less Rogue Trader", "very, very, very abstract, artistic, grotesque", "very, very indie", "make it real funky" | isometric tile maps; the painterly pass when it is registered (film until then) | the painterly pass (PR #55); depth-sorted sprites; reference frames from the author |
| [pixel-2.5d](pixel-2.5d/) | "pixel art, 2.5D, somewhat semi 3D" | orthogonal tile maps at two depths for parallax; PICO-8 palette with ordered dither | pixel-perfect scaling (M2 criterion 7); sprites; real tiles |
| [retro-thriller](retro-thriller/) | "more like Disco Elysium and less like horror ... I want it to be scary ... not ... focused entirely on horror and on jump scares and fear" | a tile map, an unsteady practical light, colour negative film, a soft tube | sprites, input (G1), real art |

Run one in the live player:
```sh
python scripts/motion_render.py --root . --serve 8765
# then open http://localhost:8765/docs/motion/index.html?scene=../../templates/pixel-2.5d/scene.mjs
```
Or render a clip: `python scripts/motion_render.py --root . --scene ../../templates/pixel-2.5d/scene.mjs --out clip.mp4`.

`tests/web/templates_check.py` renders two seconds of each template twice. It requires frames that are not blank and identical between the two runs, and it runs in CI. The finished templates' checks are ROADMAP M2 criterion 12: each builds and passes its style checks on D3D12 and WebGPU, and passes the author's contact-sheet review.
