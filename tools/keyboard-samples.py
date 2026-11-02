#!/usr/bin/env python3
"""Cuts the strokes of a CPC 464's keyboard out of a recording, for
src/app/resources/keyboard.raw.

    keyboard-samples.py recording.wav keyboard.raw [listen.wav]

The recording is the ten seconds from 2:20 of "ASMR Suono Tastiere di Retro
Computer Vintage anni 80" (YouTube IkPSJ533BWQ), cut with
    ffmpeg -ss 140 -t 10 -i <the mp3> -c:a pcm_s16le recording.wav
The times below are milliseconds into those ten seconds, found by eye on
the waveform: the onset of each stroke (the finger's touch comes some 8 ms
before the plunger strikes) and of each release.

keyboard.raw holds the takes one after the other, little-endian: a byte for
the kind (0 a letter, 2 the space bar), a byte 1 for a key going down and 0
for one coming up, two bytes 0, four bytes for the number of samples, then
the samples, 16 bits, mono, 44100 a second.
"""
import struct
import sys
import wave

import numpy as np

# A letter going down: onset, and when its release begins.
LETTER_DOWN = [(2991.0, 3073.2), (3207.2, 3249.4), (3397.0, 3469.6), (3759.0, 3842.7), (3864.7, 3953.0),
               (4138.6, 4210.1), (4852.2, 4893.6), (4949.0, 4987.0), (5082.7, 5153.3), (6125.6, 6174.5),
               (6341.6, 6449.1), (6977.6, 7038.1), (8425.1, 8535.2), (8753.4, 8822.5), (8875.3, 8928.3),
               (8966.9, 8999.3), (9065.9, 9213.4), (9221.0, 9284.4), (9572.5, 9665.1), (9899.0, 9992.4)]
# A letter coming up: the cap against its stop, and the rattle after it,
# up to the next key going down.
LETTER_UP = [(3073.2, 3207.2), (3249.4, 3397.0), (3469.6, 3593.8), (3953.0, 4138.6), (4210.1, 4852.2),
             (4893.6, 4949.0), (4987.0, 5082.7), (5153.3, 5289.2), (6174.5, 6205.9), (6449.1, 6486.3),
             (7038.1, 7136.3), (8535.2, 8753.4), (8822.5, 8875.3), (8928.3, 8966.9), (8999.3, 9065.9),
             (9284.4, 9389.9), (9665.1, 9899.0)]
# The space bar, down and up: louder, the bar and its wire ringing longer.
SPACE_DOWN = [(3593.8, 3669.8), (5756.2, 5799.7), (7136.3, 7215.5), (7983.1, 8114.0), (9389.9, 9476.5)]
SPACE_UP = [(3669.8, 3759.0), (5799.7, 6125.6), (7215.5, 7451.1), (8114.0, 8425.1), (9476.5, 9572.5)]


def take(m, fs, start, end, longest):
    end = min(end - 2.0, start + longest)
    a = int((start - 1.5) / 1000 * fs)
    b = int(end / 1000 * fs)
    s = m[a:b].copy()
    fade_in = int(0.0005 * fs)
    s[:fade_in] *= np.linspace(0, 1, fade_in)
    fade_out = len(s) * 3 // 10
    s[-fade_out:] *= np.cos(np.linspace(0, np.pi / 2, fade_out)) ** 2
    return s


def main():
    w = wave.open(sys.argv[1])
    fs = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, w.getnchannels()).astype(float)
    m = x.mean(1)
    m -= np.convolve(m, np.ones(441) / 441, "same")  # no rumble under 100 Hz
    takes = []
    for kind, down, cuts, longest in ((0, 1, LETTER_DOWN, 70), (0, 0, LETTER_UP, 50),
                                      (2, 1, SPACE_DOWN, 110), (2, 0, SPACE_UP, 70)):
        for start, end in cuts:
            takes.append((kind, down, take(m, fs, start, end, longest)))
    # The loudest stroke of each kind keeps 16 bits' worth; the others are
    # as much softer as they were played.
    loudest = max(np.abs(s).max() for _, _, s in takes)
    with open(sys.argv[2], "wb") as f:
        for kind, down, s in takes:
            q = np.clip(np.round(s / loudest * 32000), -32768, 32767).astype("<i2")
            f.write(struct.pack("<BBHI", kind, down, 0, len(q)))
            f.write(q.tobytes())
    if len(sys.argv) > 3:
        gap = np.zeros(int(0.25 * fs))
        out = np.concatenate([np.concatenate([s / loudest * 32000, gap]) for _, _, s in takes])
        o = wave.open(sys.argv[3], "wb")
        o.setnchannels(1)
        o.setsampwidth(2)
        o.setframerate(fs)
        o.writeframes(np.clip(out, -32768, 32767).astype("<i2").tobytes())
    for kind, down, s in takes:
        print(("letter", "", "space")[kind], ("up", "down")[down], f"{len(s) / fs * 1000:.0f} ms",
              f"peak {np.abs(s).max() / loudest:.2f}")


main()
