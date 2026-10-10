<!-- writing-profile: research -->
# Sound for the explainers: design

The sound engine in `web/sound/` gives raw-native films and interactive pages a voice-first mix: the narration, a generated score bed, short procedural cues, and a mastering chain. One sheet of cues drives both the offline render and live playback in the browser, and the two agree within one least significant bit. This document says what sound is for in an explainer, what the evidence on learning and perception supports, the rules the engine sets from that evidence, and how the engine is built and checked.

## 1. What sound is for

An explainer asks the viewer to follow a chain of reasoning while a voice speaks. Sound can help in three ways: it tells the viewer where to look when something changes, it marks where one part of the argument ends and the next begins, and it gives weight to a number as it lands. Sound can also hurt. A second stream of audio competes with the voice for the same channel, and decoration pulls attention toward itself.

The author's brief asked for cues "that touch on key standout areas in media in which sound can be used to maintain viewer attention." This engine reads attention as orientation. The viewer should always know what just changed and where the argument stands. It does not read attention as retention. The design canon says the work returns people to their time and must not hold attention for its own sake. So the engine has no risers that build suspense before a reveal, no stinger to sell a claim, no swell under a conclusion, and no sound whose only job is to keep someone watching.

## 2. What the evidence says

Read status is given for every source: **full** (full text read), **abstract**, or **secondary** (another author's summary). All were read on 9 October 2026 unless noted.

### 2.1 Sound under narration can hurt learning

- **Moreno and Mayer (2000)**, J. Educational Psychology 92(1), 117-125, doi:10.1037/0022-0663.92.1.117. Full. Two experiments, 75 students each, narrated animations of 180 s (lightning) and 45 s (brakes). A music loop under the narration lowered retention and transfer in both. In experiment 1, seven natural sounds, each tied to one animation event and played once, did not significantly hurt. In experiment 2, two mechanical sounds repeated at several points did hurt transfer. The authors offer coordination and repetition as an explanation and say they did not manipulate it. Limits: one bland synthesized loop, short lessons, college students, no level manipulation, F and p only.
- **Kämpfe, Sedlmeier and Renkewitz (2011)**, Psychology of Music 39(4), 424-448, doi:10.1177/0305735610376261. Full. Meta-analysis of 97 effects in adults. The overall effect of background music was null. Reading comprehension r = -.11 (8 studies), other memory tasks r = -.09. None of the cells is a narrated explainer.
- **de la Mora Velasco, Chen, Hirumi and Bai (2023)**, Psychology of Music 51(6), 1598-1626, doi:10.1177/03057356231153070. Abstract. Forty-seven studies, mean d = +0.31 in favour of background music, largest when music played before the test. This conflicts with the two sources above. I could not read how many of its comparisons put music under a voice, so it does not settle the question.
- **Vasilev, Hitching and Tyrrell (2023)**, J. Cognitive Psychology 36(1), doi:10.1080/20445911.2023.2209346. Preprint read in part. Music with lyrics slowed reading in three of four experiments; instrumental versions of the same songs did not. Tested on reading, not on listening to narration.

### 2.2 A voice needs room

- **Torcoli, Freke-Morin, Paulus and Simon (2019)**, JAES 67(12), 1003-1011, doi:10.17743/jaes.2019.0052. Abstract. Twenty-two listeners set the level of TV commentary over background. The authors recommend at least 10 LU between commentary and music and at least 15 LU over ambience. Non-experts wanted about 4 LU more than experts. This measures preference, not comprehension, and the voices were human.

### 2.3 A sound in sync with a change helps people find the change

- **Van der Burg, Olivers, Bronkhorst and Theeuwes (2008)**, JEP: Human Perception and Performance 34(5), 1053-1065, doi:10.1037/0096-1523.34.5.1053. Full. A 60 ms tone with no spatial information, played as a target changed, cut visual search slopes from 147 to 31 ms per item. The benefit held for tones within about 100 ms of the change and was gone at 150 ms. A tone that warned in advance did not help in the same way. Samples were 6 to 9 people per experiment and the display was flickering lines, so this shows faster detection in a lab, not learning from a film.
- **Vroomen and Keetels (2010)**, Attention, Perception and Psychophysics 72(4), 871-884, doi:10.3758/APP.72.4.871. Read in part. People notice sound arriving early sooner than sound arriving late. **ITU-R BT.1359** (secondary): detectability thresholds about 45 ms for audio early and 125 ms for audio late.

### 2.4 Signalling helps; the tested signals are visual or verbal

- **Mautone and Mayer (2001)**, J. Educational Psychology 93(2), 377-389, doi:10.1037/0022-0663.93.2.377. Abstract. Previews, headings and spoken pointer words raised transfer in three experiments, including narrated animation.
- **Richter, Scheiter and Eitel (2016)**, Educational Research Review 17, 19-36, doi:10.1016/j.edurev.2015.12.003. Abstract. Twenty-seven studies, r = .17 for comprehension, largest for learners with little prior knowledge. The cues were colour and arrows linking text and picture.
- **Schneider, Beege, Nebel and Rey (2018)**, Educational Research Review 23, 1-24, and the seductive-details meta-analyses by **Rey (2012)** and **Sundararajan and Adesope (2020)**: not reached. Their effect sizes appear only in secondary summaries, and this document does not cite them.

### 2.5 Earcons

- **Brewster, Wright and Edwards (1993)**, Proc. CHI '93, 222-227, doi:10.1145/169059.169179. Abstract. Structured earcons were recognized better than unstructured bursts, and musical timbres better than simple tones.
- **McGookin and Brewster (2004)**, ACM Trans. Applied Perception 1(2), 130-155, doi:10.1145/1024083.1024087. Abstract. Listeners identify fewer concurrent earcons better; a distinct timbre for each and a 300 ms onset gap helped.
- Not found: how many distinct earcons a viewer can learn in one short film, and whether repeated cues habituate or annoy over three minutes.

### 2.6 Loudness standards and platforms

- **ITU-R BS.1770-5 (11/2023)**. Read. K-weighting, 400 ms blocks with 75% overlap, an absolute gate at -70 LUFS and a relative gate 10 LU down; true peak by 4x oversampling with the order-48 interpolator in Annex 2.
- **EBU Tech 3341 v4 (11/2023)** and **Tech 3342 v4 (11/2023)**. Read. The meter test cases used in section 5.
- **EBU R 128 v5 (11/2023)**: -23.0 LUFS for broadcast, true peak at most -1 dBTP. **R 128 s2** (streaming): an interim distribution range of -20 to -16 LUFS where loudness metadata is not used. Read.
- **Apple Podcasts** audio requirements: about -16 LKFS within 1 dB, true peak at most -1 dBFS. Read. **Spotify**: normalizes music to -14 LUFS and recommends true peak at most -1 dBTP. Read. **YouTube**: its official page could not be reached; a secondary source says it turns loud uploads down to about -14 LUFS and never turns quiet ones up.
- **superstack SPEC section 8.4**, the contract this engine implements: speech at -16 LUFS within 1 LU and at most -1.5 dBTP.

### 2.7 What the best explanatory media do

The evidence here is thin. Kurzgesagt says each video gets its own composed soundtrack and that the voiceover sets the animation's timing (kurzgesagt.org/youtube, read). 3Blue1Brown credits a composer and treats audio quality as a craft problem in its FAQ (3blue1brown.com/faq, read). Ciechanowski's "Sound" article plays audio only when the reader clicks, presses a key or drags a slider, and warns the reader to check the volume first (ciechanow.ski/sound, read). Statements from Vox, Primer and Nicky Case on sound were not found. The engine takes two things from this: the voice leads and the score follows the structure, and interactive sound starts from the reader's own action.

## 3. Rules the engine sets

Each rule is a default in the engine. The source column says what supports it and how strongly.

| Rule | Engine default | Support |
|---|---|---|
| The voice leads. A score bed, if any, sits far under it. | Bed 12 LU under the narration in the gaps; ducked to 18 LU under while the voice speaks. A sheet may drop the score. | Torcoli 2019 sets a 10 LU floor and finds non-experts want about 4 more. Moreno and Mayer 2000 found a bed under narration hurt. 18 LU is my margin above both, an inference. |
| Instrumental only. | The score is sine pads; no voices or words. | Vasilev 2023 (reading only). |
| One cue per event, tied to the event. | Every cue in a sheet must say what it marks (`why`); the validator refuses a cue without one. | Moreno and Mayer 2000 experiment 1 (one-to-one sounds did not significantly hurt). Weak: not significant is not "helps". |
| Cues land with the picture, never ahead of it. | Cue time is the scene's own cue time, rounded once to a sample; at 30 fps a frame is exactly 1,600 samples, so a cue on a frame boundary is exact. | Van der Burg 2008 (benefit within about 100 ms; a warning tone did not help the same way); BT.1359 and Vroomen and Keetels on early audio. |
| Few cues, and no arbitrary repeats. | The film 1 sheet has 22 cues in 177 s. A sound repeats only when its meaning repeats (the hot-mark motif); otherwise pitch or type changes. | Moreno and Mayer 2000 experiment 2 (repeated sounds hurt). That a fixed motif with a fixed meaning acts as an earcon and not as noise is my inference from Brewster 1993. |
| Many marks make one texture. | `texture` caps its grains (default 240) whatever the count; level does not grow with the count. | Media bible 4.5. McGookin and Brewster 2004 on concurrent sounds. |
| Effects stay under the voice. | A cue at 0 dB peaks 6 dB under the narration's sample peak; cues are high-passed at 120 Hz. | Media bible 4.5. The high-pass is engineering judgement. |
| Space follows the picture. | Pan follows the marked object's x, equal-power, scaled by 0.6 so nothing sits hard left or right. | Equal-power panning keeps loudness constant across positions (tested). The 0.6 width is judgement, so the mix holds on one speaker. |
| Silence is a cue. | `score.silences` empties the bed for predict beats and recall questions. | Media bible 4.5. No direct evidence found. |
| One loudness for the web. | -16 LUFS integrated (resolved to within 0.1 LU), true peak at most -1.5 dBTP. | superstack 8.4; Apple Podcasts -16; R 128 s2 interim range -20 to -16. The ceiling is 0.5 dB under the Apple and Spotify guidance. |
| Sound only on request. | The live host never autoplays; pages give a label and a keyboard stop; reduced sound plays silence. | superstack SPEC 8.5; Ciechanowski's practice. |

What the evidence does not support, so this document does not claim it: that cue sounds raise learning (no study found tests non-speech cues on a learning outcome in narrated animation); that music always hurts (see de la Mora Velasco 2023); and that 10 LU is enough for every listener.

## 4. The engine

```
sheet (superstack.sound/1 scene: cues, score, buses, master)
  |
  +-- dialog bus: narration WAV, mono, centred ------------------+
  +-- music bus:  Score (pure in sample index) x duck gain -------+--> sum --> EQ --> compressor --> gain --> true-peak limiter --> out
  +-- sfx bus:    cue buffers placed at their sample, panned -----+
                  ducker keyed from the narration 150 ms ahead --^
```

| File | Job |
|---|---|
| `dsp.mjs` | Wavetable sine read at a phase from the absolute sample index, RBJ biquads, equal-power pan |
| `cues.mjs` | Nine procedural cue voices; no samples, so nothing to license |
| `score.mjs` | The score bed, one chord per section; every sample is a pure function of its index |
| `master.mjs` | EQ, a log-domain compressor (Giannoulis, Massberg and Reiss 2012, JAES 60(6)), the true-peak limiter |
| `mix.mjs` | Buses, ducking, cue scheduling; `process(left, right, n)` and `seek(n)` |
| `meter.mjs`, `truepeak-fir.mjs` | Integrated, momentary, short-term, loudness range, true peak |
| `offline.mjs`, `render.mjs` | Resolve levels, render, report, superstack receipt |
| `live.mjs`, `worklet.mjs` | The browser host: an AudioWorklet running the same `Mix` |
| `contract.mjs` | The superstack example scene through the mixer |

**Determinism.** Everything is float64 arithmetic in plain loops in index order. Noise comes from the superstack seed rule (xmur3 and mulberry32), one substream per cue. Sources are pure functions of the sample index; everything with memory steps one sample at a time. Block size therefore cannot change the output, and a test renders the same sheet at blocks of 128, 777 and 4096 and compares hashes.

**Offline and live.** No WebAudio node touches the signal: browser filters and compressors differ between engines. The AudioWorklet runs the same `Mix` class, 128 samples at a time, and WebAudio only carries the samples to the output. The worklet's output buffers are float32, so the live path differs from the float64 render only by that rounding. Stated tolerance: the superstack PCM bound, at most 2 LSB and SNR at least 60 dB. Measured: at most 1 LSB (section 5). Narration WAVs are parsed by the engine's own reader, not `decodeAudioData`, and s16 values map to `v / 32768`, which float32 holds exactly.

**Seeking.** A seek rebuilds all state by replaying the 4 s before the target, then plays on. The ducker's hold and release, the compressor and the limiter all settle within that time. This is checked, not assumed (section 5).

**Ducking.** The ducker keys from the narration 150 ms ahead of the music it gains, holds for 350 ms so the bed does not pump between words, and moves with a 60 ms attack and a 700 ms release. Narration is a known buffer in both hosts, so the lookahead costs no latency.

**True-peak limiter.** For each new sample the limiter computes the largest magnitude among the sample itself and its four Annex 2 interpolated values, and the gain that would bring it to the ceiling. A sliding minimum over W + 11 samples, then a box average over W samples, gives a gain that has reached the target before the peak leaves the delay line, for every interpolated value that depends on that sample. The delay is W + 10 samples (106 at W = 96, 2.2 ms). Release is a one-pole of 80 ms. The ceiling sits 0.3 dB under the stated ceiling to cover the gain's slope across the interpolator; the meter confirms the result.

**Levels are resolved offline.** `resolve()` measures the narration and the score, sets the music gain, the duck depth and the effects gain from the rules in section 3, and iterates the master gain until the mastered mix reads the target. It writes every number into `sheet.resolved.json`. The live player plays that file and measures nothing, so live playback is at the levels the offline render was checked at.

**Scenes declare their cues.** A Motion scene may add a `sound(ctx)` method that returns `[{ t, type, why, x?, ... }]` in seconds, computed from the same timeline its `frame()` uses (`ctx.assets` is what `load` returned). `scene-sheet.mjs` loads the scene in Node with no renderer and turns those cues into a sheet, with the score changing chord at each chapter. A scene without `sound()` gets one `chapter` cue per chapter. "Checking the light" marks its two fields gathering, the merge, the comparison and the verdict; a refuted verdict gets the hot-mark motif. A walkthrough plucks once as each command starts, a step higher each time, and plays the motif on a non-zero exit.

**In the release loop.** When `raw-native media render` has narration for a scene, it builds the sheet from the bundled scene, renders the mix, checks the loudness verdict, and muxes the mix in place of the bare narration. The bundle keeps `sound/sheet.json`, `sheet.resolved.json`, `report.json` and `receipt.json`, and the manifest records the loudness, true peak, music under speech, cue count and PCM hash. A mix that misses its loudness target stops the render. `--no-sound` restores the bare narration. The hosted CI renders have no narration and stay silent.

## 5. Checks and measurements

Tests (`node --test web/sound/sound.test.mjs`, 11 tests, about 6 s):

- The mixer renders the superstack example scene to the contract's reference PCM, `692ead20...`, bit for bit, at two block sizes. This is the roadmap's M1 media item: a sample-exact offline mix against the `superstack.sound/1` vectors.
- Integrated loudness matches every superstack loudness vector within 1e-6 LU (measured agreement about 1e-13).
- Tech 3341 cases 1 to 5, 9 and 12 read -23.0 within 0.1 LU; cases 15 to 19 read true peak within +0.2/-0.4 dB. Cases 6 to 8, 10, 11, 13, 14 and 20 to 23 are not run: 6 needs five channels, 7 and 8 need the EBU's programme files, and 20 to 23 need a downsampling filter the document does not specify.
- Tech 3342 cases 1 to 4 read loudness range within 1 LU.
- The fixture render is identical across block sizes and equals a pinned hash, which CI checks on Ubuntu and Windows.
- The float32 live path and a mid-sheet seek stay within the PCM tolerance.
- Resolved mastering reads -16 LUFS within 0.1 LU with true peak at most -1.5 dBTP, music at least 16 LU under speech, effects at least 6 dB under the voice's peak.
- The limiter holds a +3 dBTP signal under its ceiling; a control shows the same signal reads over +2.5 dBTP without it.

In the browser (`scripts/sound_live_check.py`, Chrome 154 headless, OfflineAudioContext with the worklet, 9 October):

| Sheet | Span | max_abs_lsb | exact samples | SNR |
|---|---|---|---|---|
| Fixture, 10 s | whole | 1 | 99.997% | 114.3 dB |
| Film 1, 177.5 s | whole | 1 | 99.995% | 114.2 dB |
| Film 1 | seek to 90 s, 20 s | 1 | 99.995% | 114.7 dB |

The worklet rendered the 177.5 s film in 5.5 s of wall time, about 32 times real time, so live playback has wide headroom. CI runs the fixture check in the media-smoke job.

Film 1 mix, this engine's meter and ffmpeg 7.1 `ebur128=peak=true`:

| Measure | Engine | ffmpeg |
|---|---|---|
| Integrated | -16.00 LUFS | -16.0 LUFS |
| Loudness range | 2.85 LU | 2.8 LU |
| True peak | -1.80 dBTP | -1.8 dBFS (true peak) |

## 6. Choices in use

On 10 October 2026 the author said "continue" without picking between the mixes on the review page. The coordinator then shipped this choice, which is reversible:

| Where | Mix | What it is |
|---|---|---|
| Concept films (film 1 now; films 2 to 5 when re-rendered) | C | Narration and cues; no score bed under the voice |
| Release media (raw-native v0.6.0's two videos, and the release loop) | B | Narration, cues, and a score ducked to 18 LU under the voice |

C for the concept films follows section 2.1: the one controlled study of music under narration found that it hurt. B stays for release media, which are short and walk through what a command prints. That split is a judgement, and no study separates the two kinds of piece.

Every choice can be switched back by swapping the mix file:

- **Film 1 on the site.** Restore `checking-cost.mp4` and `checking-cost.m4a`, and their output hashes in `film.receipt.json`, from the commit before the change. That brings back mix A. Alternatively, build the sheet with `film.sound.mjs --with-score` and render it to get mix B. The interactive version plays `checking-cost.m4a`, so it follows the file.
- **Release assets.** The videos and zips first attached to v0.6.0 carried the narration alone. They are kept, and re-attaching them with `gh release upload --clobber` restores them. A future render takes `--no-sound` for the bare narration. Removing a scene's `sound()` method gives it chapter cues only.
- **A sheet.** Adding or removing `score` is the whole difference between B and C.

## 7. Limits and open questions

- Nothing here shows that the cues help anyone understand the films. The right test is a within-film comparison, cue against no cue, with a retention and transfer question afterwards; no study I found has done it for non-speech cues under narration.
- The narration is synthesized. Torcoli's listeners heard human commentary, and masking margins may differ for a synthetic voice.
- Habituation over a three-minute film is unknown. The engine limits repeats; it cannot measure annoyance.
- Live-path identity holds for one Chrome version. Firefox and Safari use different JavaScript engines; their `Math.sin`, `Math.exp` and `Math.pow` may differ in the last bit, which the s16 tolerance absorbs but the tests here have not measured.
- A PCM hash says nothing about how a device plays the sound.
