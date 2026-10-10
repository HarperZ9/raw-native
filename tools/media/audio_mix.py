#!/usr/bin/env python3
"""The offline audio mix for Motion media: sample-exact with web/motion/audio.mjs and with
the superstack.sound/1 rules (s16 quantization, the superstack-bs1770/1 loudness meter).

    python tools/media/audio_mix.py OUT.wav --track narration.wav --track score.wav@-13 \\
        [--channels 2] [--seconds 177.4667] [--rate 48000]

Tracks are 16-bit PCM WAV files, mono or with the output's channel count, at the output
rate. A mono track feeds every channel. "@G" scales a track by G dB; "x" instead of "@"
gives a linear factor ("score.wav x0.2238"). Sums run in float64, track by track, then
quantize by superstack's rule. Prints the mix's integrated loudness, sample peak and the
SHA-256 of its PCM as JSON. Standard library only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import wave
from array import array
from pathlib import Path

RATES = (8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000)


def quantize_s16(samples) -> array:
    out = array("h", bytes(2 * len(samples)))
    for i, x in enumerate(samples):
        v = max(-1.0, min(1.0, x))
        out[i] = math.floor(v * 32767.0 + 0.5)
    return out


def read_wav(path: Path) -> tuple[list[float], int, int]:
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: only 16-bit PCM is read")
        rate, ch = w.getframerate(), w.getnchannels()
        pcm = array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        pcm.byteswap()
    # v / 32767 is superstack's s16ToFloats: it makes read-then-quantize the identity,
    # so an already-mastered s16 track at gain 1 passes through bit for bit.
    return [v / 32767.0 for v in pcm], rate, ch


def mix_tracks(tracks: list[dict], channels: int, frames: int) -> list[float]:
    out = [0.0] * (frames * channels)
    for t in tracks:
        g, off, tc, s = t["gain"], t.get("offset", 0), t["channels"], t["samples"]
        if tc not in (1, channels):
            sys.exit(f"a {tc}-channel track cannot mix into {channels} channels")
        n = len(s) // tc
        for i in range(n):
            f = i + off
            if f < 0 or f >= frames:
                continue
            base = f * channels
            for c in range(channels):
                out[base + c] += s[i * tc + (0 if tc == 1 else c)] * g
    return out


def k_weighting(rate: int):
    if rate == 48000:
        return ((1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585),
                (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621))
    fs = float(rate)
    k, q = math.tan(math.pi * 1681.974450955533 / fs), 0.7071752369554196
    vh = 10.0 ** (3.999843853973347 / 20.0)
    vb = vh ** 0.4996667741545416
    a0 = 1.0 + k / q + k * k
    shelf = ((vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
             2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0)
    k, q = math.tan(math.pi * 38.13547087602444 / fs), 0.5003270373238773
    a0 = 1.0 + k / q + k * k
    return shelf, (1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0)


def _biquad(x, c):
    y = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    b0, b1, b2, a1, a2 = c
    for i, v in enumerate(x):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def _loud(p: float) -> float:
    return -0.691 + 10.0 * math.log10(p) if p > 0 else -math.inf


def integrated_lufs(samples, rate: int, channels: int = 1):
    if channels not in (1, 2) or rate not in RATES or rate % 10:
        return None
    filt = k_weighting(rate)
    chans = [_biquad(_biquad(samples[c::channels], filt[0]), filt[1]) for c in range(channels)]
    n, block, hop = len(chans[0]), rate * 4 // 10, rate // 10
    # Running sums of squares make each 400 ms block O(1).
    cums = []
    for z in chans:
        acc, cum = 0.0, [0.0]
        for v in z:
            acc += v * v
            cum.append(acc)
        cums.append(cum)
    powers = []
    i = 0
    while i + block <= n:
        powers.append(sum((cum[i + block] - cum[i]) / block for cum in cums))
        i += hop
    gated = [p for p in powers if _loud(p) > -70.0]
    if not gated:
        return None
    rel = _loud(sum(gated) / len(gated)) - 10.0
    kept = [p for p in gated if _loud(p) > rel]
    return _loud(sum(kept) / len(kept))


def peak_dbfs(samples):
    peak = max((abs(v) for v in samples), default=0.0)
    return 20.0 * math.log10(peak) if peak > 0 else None


def write_wav(path: Path, pcm: array, rate: int, channels: int) -> None:
    if sys.byteorder == "big":
        pcm = array("h", pcm)
        pcm.byteswap()
    data = pcm.tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)


def parse_track(spec: str) -> tuple[str, float]:
    if "@" in spec:
        p, db = spec.rsplit("@", 1)
        return p, 10.0 ** (float(db) / 20.0)
    if " x" in spec:
        p, g = spec.rsplit(" x", 1)
        return p, float(g)
    return spec, 1.0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--track", action="append", required=True)
    ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--seconds", type=float)
    a = ap.parse_args(argv)
    tracks = []
    for spec in a.track:
        path, gain = parse_track(spec)
        s, rate, ch = read_wav(Path(path))
        if rate != a.rate:
            sys.exit(f"{path}: {rate} Hz, the mix is {a.rate} Hz")
        tracks.append({"samples": s, "channels": ch, "gain": gain})
    frames = round(a.seconds * a.rate) if a.seconds else max(len(t["samples"]) // t["channels"] for t in tracks)
    mix = mix_tracks(tracks, a.channels, frames)
    pcm = quantize_s16(mix)
    write_wav(Path(a.out), pcm, a.rate, a.channels)
    info = {"frames": frames, "rate": a.rate, "channels": a.channels, "integrated_lufs": integrated_lufs(mix, a.rate, a.channels),
            "peak_dbfs": peak_dbfs(mix), "pcm_sha256": hashlib.sha256(pcm.tobytes()).hexdigest()}
    print(json.dumps(info))
    return 0


if __name__ == "__main__":
    sys.exit(main())
