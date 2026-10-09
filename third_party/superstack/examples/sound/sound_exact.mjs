// SPDX-License-Identifier: FSL-1.1-MIT
// The second sound path: the same score in JavaScript float64, run in Node or a
// browser. Usage: node sound_exact.mjs sound.json OUTDIR
import * as ss from '../../superstack.mjs';

export function score(scene) {
  const r = ss.rng(scene.seed);
  return Array.from({ length: scene.voices }, (_, i) => ({
    start: i * scene.step_samples, freq: scene.scale_hz[Math.floor(r.nextFloat() * scene.scale_hz.length)] }));
}

function env(k, L, A, R, g) {
  if (k < 0 || k >= L) return 0;
  if (k < A) return g * k / A;
  if (k < L - R) return g;
  return g * (L - k) / R;
}

export function render(scene) {
  const out = new Float64Array(scene.duration_samples);
  const { note_samples: L, attack_samples: A, release_samples: R, voice_gain: g, rate } = scene;
  for (const note of score(scene)) {
    const w = 2 * Math.PI * note.freq / rate;
    for (let k = 0; k < L && note.start + k < out.length; k++) out[note.start + k] += env(k, L, A, R, g) * Math.sin(w * k);
  }
  return out.map((x) => x * scene.master_gain);
}

const isMain = typeof process !== 'undefined' && Boolean(process.argv && process.argv[1])
  && (await import('node:url')).pathToFileURL(process.argv[1]).href === import.meta.url;
if (isMain) {
  const { readFileSync, writeFileSync, mkdirSync } = await import('node:fs');
  const { join } = await import('node:path');
  const [scenePath, outdir] = process.argv.slice(2);
  const scene = JSON.parse(readFileSync(scenePath, 'utf8'));
  const x = render(scene);
  const pcm = ss.quantizeS16(x);
  mkdirSync(outdir, { recursive: true });
  writeFileSync(join(outdir, 'pcm.s16'), pcm);
  const lufs = ss.integratedLufs(x, scene.rate, scene.channels), peak = ss.peakDbfs(x);
  const rec = ss.makeReceipt({
    producer: 'superstack-example-sound-js', version: '0.1.0', backend: 'js-f64', scene, content: pcm,
    media: { kind: 'audio', content: 'music', rate: scene.rate, channels: scene.channels, format: 's16le', frames: x.length,
      duration_flicks: x.length * ss.flicksPerSample(scene.rate), meter: ss.METER, integrated_lufs: ss.round6(lufs),
      peak_dbfs: ss.round6(peak), loudness_class: 'interactive', loudness_verdict: ss.loudnessCheck('interactive', lufs, peak),
      access: { autoplay: false, captions: null, transcript: false, reduced_sound: 'silent' } },
    outputs: { 'pcm.s16': ss.sha256(pcm) },
    doesNotProve: ['A second implementation of the same score; it shares the algorithm, not the runtime.',
      'A PCM hash says nothing about how a device plays the sound.'],
  });
  const errs = ss.verifyReceipt(rec);
  if (errs.length) throw new Error('receipt invalid: ' + errs.join(','));
  writeFileSync(join(outdir, 'receipt.json'), ss.canonical(rec));
  console.log(rec.content_sha256, score(scene).map((n) => n.freq));
}
