# ADR 0012: the media engine and the release loop

**Status:** Accepted. **Date:** 2026-10-09. **Builds on:** [0011](0011-motion.md).

## Context

The author's direction, 2026-10-09: "We want to make a full scale rendering
engine with educational videos, and allow this engine to become a part of our
rollouts; it will build and generate our media content." And: "There are no
explainer videos and walkthroughs".

Two facts limit the design:
- GitHub's hosted runners have no GPU.
- The author's voice model lives only on the author's machine.

Motion (ADR 0011) already draws a scene to video and plays it live. What it
lacks is a way to run on every release with no hand work, from values the
release itself produces, so that a video cannot describe a different version
from the one it ships with.

## Decision

**A media spec per repo, one command, and a release workflow split by what
needs the author's machine.**

### 1. The spec lives in each repo

`docs/media/media.json` lists the repo's scenes, and the scene modules sit
beside it in `docs/media/scenes/`. Each scene is one of two kinds:

- **explainer**: a Motion scene module, as for the films.
- **walkthrough**: a list of steps, each with the words to say and the
  commands to run. The engine supplies the scene (`web/motion/walkthrough.mjs`):
  - a terminal that types each command and shows the output it really printed;
  - a camera that moves to the lines the spec marks;
  - captions from the words.

Each scene names its **facts**: values read at render time from the checkout.
A fact is one of:
- `cmd`: a command's output, with an optional regex;
- `file` + `pointer`: a JSON value at a JSON pointer;
- `text` + `regex`: a match in a file.

The render writes `facts.json` with each value, its source and the commit, and
the scene reads its numbers only from there. A walkthrough's terminal output is
recorded the same way, as `<scene>.cast.json`. A video rendered at a tag
therefore shows that tag's numbers and output.

### 2. One command

```
raw-native media render [SCENE ...] --spec docs/media/media.json --out DIR
    [--width 3840 --height 2160] [--adapter gpu|swiftshader] [--narration DIR]
raw-native media facts  --spec ...        # facts and recordings only
raw-native media attach --tag vX.Y.Z DIR  # upload a render to the GitHub release
```

The command is `tools/media/raw_native_media.py`, with a `raw-native` launcher
in `tools/media/`. For each scene it writes:
- the master video;
- a 1920 x 1080 web encode;
- a poster;
- WebVTT captions;
- `facts.json`;
- an interactive page: a self-contained folder with the engine, the scene and
  `index.html`, which uses the live player;
- `media.json`, listing every output with its SHA-256.

Without `--narration`, a scene is timed from its words at a fixed speaking
rate. The video is silent and carries burned-in captions as well as the VTT.
With `--narration DIR`, holding `narration.wav` and `timing.json` from the
site's narration tool, the cues follow the voice and the audio is muxed.

### 3. The release workflow, split in two

`.github/workflows/media.yml` in raw-native is a reusable workflow
(`on: workflow_call`). A repo calls it from its release workflow with its tag.

- **Hosted, on every tag:**
  - check out the repo at the tag and raw-native at a pinned commit;
  - run `media render --adapter swiftshader --width 960 --height 540`
    with no narration, one job per scene;
  - upload the outputs to the release as `media-<tag>.zip`, plus each web
    MP4 and poster.
  SwiftShader is Chrome's CPU WebGPU adapter. It draws the same WGSL as a GPU,
  only slower. Measured on 9 October 2026:
  - "Checking the light" at 960 x 540: a median 720 ms a frame on the hosted
    ubuntu-24.04 runner, against 7 ms at 1920 x 1080 on the author's RTX 4090;
  - the walkthrough at 960 x 540 on the runner: 343 ms a frame;
  - film 1 at 1280 x 720 on SwiftShader on the author's machine: 179 ms a frame.
  The first release run, for v0.6.0, rendered both scenes in one job at
  1280 x 720. The explainer took 1.8 s a frame, 42 minutes in all, and the
  job was cancelled at its 90-minute limit before the walkthrough finished.
  Each scene now renders in its own job, at 960 x 540, with a 150-minute
  limit. The runner computed the same certificate values as
  the author's Windows build (rmse 0.129436031 over 37,996 pixels).
- **Local, after the tag, under `D:/gpu.lock.d`:**
  - narrate the scene words with the author's voice model, using the site's
    `tools/explainer/film/narrate.py`;
  - run `media render --narration ... --width 3840 --height 2160` on the GPU;
  - run `media attach`, which replaces the hosted files on the release with the
    narrated ones and adds the 4K master.

A **self-hosted runner** on the author's machine would fold the local half into
the workflow. Registering one gives GitHub jobs a path onto that machine, so it
waits for the author's explicit yes. Until then the local half is one command.

### 4. The site

Release assets have stable URLs
(`https://github.com/<owner>/<repo>/releases/download/<tag>/<file>`). The
site's media pages link the current release's files and host the interactive
folder. A cross-repo push from a release would need a token with write access
to the site, so for now the site picks up new media in its own PR. That step
can become automatic once the author grants such a token.

## Consequences

- A hosted render takes tens of minutes of CPU for a minute of video. Scenes stay short, and the 4K master is made only locally.
- A fact that fails to resolve fails the render. A video never ships a number
  it could not read.
- Walkthrough commands run on the runner at the tag. They must be safe to run
  there: no network writes, no secrets.
- Hosted videos are silent with burned-in captions until the local narration
  is attached. The release notes say which state a release is in.

## Alternatives

- **Render everything locally and upload by hand:** no drift check, and media
  falls behind releases.
- **A cloud GPU runner:** costs money per release, and the voice model would
  have to leave the author's machine.
- **Screen recordings of a real terminal:** honest, but they cannot be restyled,
  scrubbed or made interactive. The walkthrough records the same output and
  draws it.

## Reversal signal

If a hosted render routinely takes longer than the release job's budget
(about 30 minutes), move the hosted half to a self-hosted runner, with the
author's yes, or to a GPU runner.
