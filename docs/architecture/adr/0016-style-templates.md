# ADR 0016: three starter templates as the style acceptance corpus

**Status:** Accepted. **Date:** 2026-10-09. **Amends:** [ROADMAP.md](../ROADMAP.md)
(M2 criteria). **Builds on:** 0014.

## Context

The styles-and-genres revision proposed one playable template per named style (gap
S12) as the acceptance corpus for the stylised passes. The author chose the first three
on 2026-10-09.

## Decision

The first three templates, each in the author's words:

1. **A painted isometric RPG.** "More Disco Elysium and less Rogue Trader", "very, very,
   very abstract, artistic, grotesque", "very, very indie", "make it real funky", "blow
   people away".
2. **Pixel art in 2.5D.** "Pixel art, 2.5D, somewhat semi 3D".
3. **A thriller with a retro look.** "More like Disco Elysium and less like horror. I
   mean, you know, I want it to be scary. I want it to be like a thriller, but I don't
   want it to necessarily be focused entirely on horror and on jump scares and fear."

Each template:
- builds and runs on D3D12 and WebGPU;
- passes the checks of the passes it uses, each with its image-difference bound set
  before the first run;
- ships in the Studio.

Its art direction is reviewed with the author on a contact sheet before its milestone
closes.

## Consequences

- M2's template criterion names these three in place of the proposed isometric, pixel
  art and PS1 horror set. The PS1 look stays available as passes (gap S5), used by the
  thriller where it serves the mood rather than as the template's subject.
- The painted look rests on inferred technique: no primary source describes how Disco
  Elysium or Zero Parades build it. Reference frames from the author are needed for
  template 1.

## What would reverse it

The author's review of a template's contact sheet.
