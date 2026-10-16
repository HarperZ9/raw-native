# Style templates

These are the three starter templates the author chose on 9 October 2026 (ADR 0016). They are M2's style acceptance corpus. Each one shows what the engine does for a kind of game, and each will become a playable starting point. For now they are **scaffolds**: they run, but their art is placeholder art made in code. The art direction is still to come. The first hand-made places for two of these styles live with the explainer guides: a painted isometric harbour (`guides/worlds/harbour.mjs`) and a pixel 2.5D tidepool with six parallax depths and its own fixed palette (`guides/worlds/tidepool.mjs`).

| Template | In the author's words | Uses today | Waits on |
|---|---|---|---|
| [painted-iso-rpg](painted-iso-rpg/) | "more Disco Elysium and less Rogue Trader", "very, very, very abstract, artistic, grotesque", "very, very indie", "make it real funky" | isometric tile maps; the painterly pass when it is registered (film until then); playable (G1): pointer picking, click to walk by the shortest path, stick walking, zoom about the pointer | the painterly pass (PR #55); depth-sorted sprites (the walker draws over blocks in front of it); reference frames from the author |
| [pixel-2.5d](pixel-2.5d/) | "pixel art, 2.5D, somewhat semi 3D" | orthogonal tile maps at two depths for parallax; PICO-8 palette with ordered dither | pixel-perfect scaling (M2 criterion 7); sprites; real tiles |
| [retro-thriller](retro-thriller/) | "more like Disco Elysium and less like horror ... I want it to be scary ... not ... focused entirely on horror and on jump scares and fear" | a tile map, an unsteady practical light, colour negative film, a soft tube; playable (G1): walk, click to walk, follow camera, zoom | sprites, real art |

Play one (keyboard, pointer or gamepad; the controls and rebinding are under the view):
```sh
python scripts/motion_render.py --root . --serve 8765
# then open http://localhost:8765/web/game/play.html?scene=../../templates/retro-thriller/scene.mjs
```
A playable template's film plays its scripted demo through the same simulation, so a capture shows play.

Run one in the live player:
```sh
python scripts/motion_render.py --root . --serve 8765
# then open http://localhost:8765/docs/motion/index.html?scene=../../templates/pixel-2.5d/scene.mjs
```
Or render a clip: `python scripts/motion_render.py --root . --scene ../../templates/pixel-2.5d/scene.mjs --out clip.mp4`.

`tests/web/templates_check.py` renders two seconds of each template twice. It requires frames that are not blank and identical between the two runs, and it runs in CI. The finished templates' checks are ROADMAP M2 criterion 12: each builds and passes its style checks on D3D12 and WebGPU, and passes the author's contact-sheet review.
