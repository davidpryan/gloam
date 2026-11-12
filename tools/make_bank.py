#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 David Ryan
"""GLOAM's vocal bank: the resource region's file (app/bank/bank.bin).

    python3.11 tools/make_bank.py                 the factory bank: sung here by a formant synthesiser
    python3.11 tools/make_bank.py VOICES/         your own: up to 16 WAV files (any rate, mono or stereo), in name order

Each slice becomes 16 kHz 8-bit mu-law (about 12 s in all fit the 196 KiB region). Its pitch is found by
autocorrelation over its loudest part, so the choir's grains lock to it. Rebuild after (./build.sh): the package
carries the bank's length and CRC, and the installer writes it after the firmware.

The format: "GLB1", u16 slices, u16 rate; 16 x {u32 offset, u32 length, u16 f0 x 10, u8 note, u8 0, char name[8]};
the samples."""
import math
import pathlib
import struct
import sys
import wave

import numpy as np

RATE = 16000
NSLICE = 16
MAX_BYTES = 48 * 4096 - 32 - 8 - NSLICE * 20
OUT = pathlib.Path(__file__).resolve().parent.parent / "app" / "bank" / "bank.bin"


def ulaw(x):
    """float -1..1 -> mu-law bytes (G.711, as the firmware decodes it)"""
    s = np.clip(np.round(x * 32635), -32635, 32635).astype(np.int32)
    sign = np.where(s < 0, 0x80, 0)
    a = np.abs(s) + 0x84
    exp = np.zeros_like(a)
    for e in range(7, 0, -1):
        exp = np.where((exp == 0) & (a >= (0x84 << e)), e, exp)
    mant = (a >> (exp + 3)) & 0x0F
    return (~(sign | exp << 4 | mant) & 0xFF).astype(np.uint8)


def pitch_of(x):
    """the fundamental (Hz) of a voice, by autocorrelation of its loudest 100 ms"""
    n = int(0.1 * RATE)
    if len(x) < n * 2:
        n = len(x) // 2
    e = np.convolve(x * x, np.ones(n), "valid")
    i = int(np.argmax(e))
    seg = x[i:i + n] - np.mean(x[i:i + n])
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    lo, hi = int(RATE / 900), int(RATE / 70)
    lag = lo + int(np.argmax(ac[lo:hi]))
    for d in (4, 3, 2):                                                          # an octave error: a shorter period as strong
        cand = int(round(lag / d))
        if cand >= lo:
            w = ac[max(lo, cand - 2):cand + 3]
            if len(w) and np.max(w) >= 0.8 * ac[lag]:
                lag = max(lo, cand - 2) + int(np.argmax(w))
                break
    return RATE / lag


# ---- the factory bank: a small formant singer (glottal pulse, five formants, breath, vibrato) ----
VOWELS = {"a": (730, 1090, 2440, 3400, 4100), "o": (570, 840, 2410, 3300, 4000), "u": (300, 870, 2240, 3200, 3900),
          "i": (270, 2290, 3010, 3600, 4200), "e": (530, 1840, 2480, 3500, 4100), "m": (250, 1200, 2200, 3200, 4000)}
BANDS = (60, 90, 120, 160, 200)


def resonate(src, f, bw):
    r = math.exp(-math.pi * bw / RATE)
    c = 2 * r * math.cos(2 * math.pi * f / RATE)
    y = np.zeros_like(src)
    y1 = y2 = 0.0
    g = 1 - r
    for i, s in enumerate(src):
        v = g * s + c * y1 - r * r * y2
        y2, y1 = y1, v
        y[i] = v
    return y


def sing(notes, vowels, dur, breath=0.08, rng=np.random.default_rng(7)):
    """a phrase: per segment a MIDI note and a vowel, glided between, with vibrato that comes in late"""
    seg = len(notes)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    k = np.minimum((t / dur * seg).astype(int), seg - 1)
    f0 = 440 * 2 ** ((np.array(notes, float)[k] - 69) / 12)
    f0 = np.convolve(f0, np.ones(800) / 800, "same")                         # glides between notes
    vib = 1 + 0.014 * np.sin(2 * math.pi * 5.3 * t) * np.clip((t - 0.25) / 0.4, 0, 1)
    ph = np.cumsum(f0 * vib / RATE) % 1.0
    src = np.where(ph < 0.55, np.sin(math.pi * ph / 0.55) ** 2, 0.0) - 0.3       # an open-close pulse
    src += breath * rng.standard_normal(n)
    out = np.zeros(n)
    for b in range(5):                                                           # the vowel's formants, interpolated
        fa = np.array([VOWELS[v][b] for v in vowels], float)[k]
        fa = np.convolve(fa, np.ones(1200) / 1200, "same")
        # a time-varying resonator, a block at a time
        y = np.zeros(n)
        blk = 256
        for s0 in range(0, n, blk):
            y[s0:s0 + blk] = resonate(src[s0:s0 + blk], float(fa[min(s0 + blk // 2, n - 1)]), BANDS[b])
        out += y / (1 + b * 0.6)
    env = np.minimum(1, t / 0.06) * np.minimum(1, (dur - t) / 0.25)
    out *= np.clip(env, 0, 1)
    return out / (np.max(np.abs(out)) + 1e-9) * 0.85


def factory():
    """sixteen phrases: long vowels and short calls, the kind a producer chops and pitches"""
    P = [("AAH", [61], "a", 0.9), ("OOH", [64], "u", 0.9), ("OH", [66], "o", 0.8), ("EE", [68], "i", 0.7),
         ("MMM", [61], "m", 0.8), ("AH-OH", [64, 61], "ao", 0.9), ("OO-AH", [68, 66], "ua", 0.9), ("EH", [69], "e", 0.6),
         ("HIGH", [73], "a", 0.7), ("LOW", [49], "o", 0.8), ("FALL", [71, 68, 64], "aoo", 1.0), ("RISE", [61, 64, 68], "uoa", 1.0),
         ("SIGH", [66, 61], "ae", 0.7), ("CALL", [68, 68, 73], "eea", 0.8), ("HUM", [56], "m", 0.7), ("OOO", [61], "u", 0.6)]
    out = []
    for name, notes, vow, dur in P:
        out.append((name, sing(notes, list(vow) if len(vow) == len(notes) else [vow[0]] * len(notes), dur)))
    return out


def load_wavs(folder):
    out = []
    for f in sorted(pathlib.Path(folder).glob("*.wav"))[:NSLICE]:
        with wave.open(str(f)) as w:
            ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
            raw = w.readframes(n)
        dt = {1: np.uint8, 2: np.int16, 4: np.int32}[sw]
        x = np.frombuffer(raw, dt).astype(float)
        if sw == 1:
            x = (x - 128) / 128
        else:
            x /= float(2 ** (8 * sw - 1))
        x = x.reshape(-1, ch).mean(axis=1)
        idx = np.arange(0, len(x), rate / RATE)                                    # to 16 kHz (linear)
        x = np.interp(idx, np.arange(len(x)), x)
        x = x / (np.max(np.abs(x)) + 1e-9) * 0.9
        out.append((f.stem.upper()[:7], x))
    return out


def build(slices):
    data, table, off = bytearray(), [], 0
    budget = MAX_BYTES // max(1, len(slices))
    for name, x in slices:
        b = ulaw(x[:budget]).tobytes()
        f0 = pitch_of(x)
        note = int(round(69 + 12 * math.log2(f0 / 440)))
        table.append(struct.pack("<IIHBB8s", off, len(b), int(f0 * 10), note, 0, name.encode()[:7].ljust(8, b"\0")))
        data += b
        off += len(b)
        print(f"  {name:8s} {len(b) / RATE:5.2f} s  {f0:6.1f} Hz  note {note}")
    while len(table) < NSLICE:
        table.append(table[len(table) % max(1, len(slices))])
    head = b"GLB1" + struct.pack("<HH", len(slices), RATE)
    return head + b"".join(table) + bytes(data)


if __name__ == "__main__":
    slices = load_wavs(sys.argv[1]) if len(sys.argv) > 1 else factory()
    if not slices:
        raise SystemExit("make_bank: no WAV files there")
    blob = build(slices)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    print(f"bank     {OUT}  {len(blob)} B of {48 * 4096 - 32} ({len(slices)} slices)")
