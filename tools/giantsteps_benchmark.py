#!/usr/bin/env python3
"""GiantSteps Key benchmark for Mode Match (PLAN.md 4b: "GiantSteps Key, scored with the MIREX weighted key score").

Runs `ModeMatchTests --analyze <file>` on every track of the GiantSteps Key dataset (604 two-minute EDM previews,
Knees et al., ISMIR 2015) and scores the shown major/minor key against the annotations:

    correct 1.0, perfect fifth 0.5, relative 0.3, parallel 0.2, other 0.0   (MIREX audio key detection)

NuvoDeck's own answer (the profile match, "nuvo key") is scored as well, so the two can be compared.

Getting the dataset (not shipped: the audio is Beatport preview MP3s):
    git clone https://github.com/GiantSteps/giantsteps-key-dataset
    cd giantsteps-key-dataset && ./audio_dl.sh          # downloads audio/*.mp3 (needs curl)

Usage (ffmpeg must be on PATH; ModeMatchTests decodes with it):
    python tools/giantsteps_benchmark.py <dataset dir> [path to ModeMatchTests] [--jobs N] [--csv results.csv]
"""
import argparse
import concurrent.futures
import csv
import os
import re
import subprocess
import sys

NOTE = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
        "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11, "Cb": 11}


def parse_key(text):
    """'F# minor' / 'Bb major' / 'Ebm' -> (pitch class, minor) or None."""
    m = re.match(r"\s*([A-G][#b]?)\s*(major|minor|maj|min|m)?\b", text.strip())
    if not m or m.group(1) not in NOTE:
        return None
    mode = (m.group(2) or "major").lower()
    return NOTE[m.group(1)], mode in ("minor", "min", "m")


def mirex(truth, guess):
    if guess is None:
        return 0.0
    (t, tm), (g, gm) = truth, guess
    if (t, tm) == (g, gm):
        return 1.0
    if tm == gm and (g - t) % 12 in (5, 7):
        return 0.5
    if tm != gm and ((not tm and g == (t + 9) % 12) or (tm and g == (t + 3) % 12)):
        return 0.3
    if g == t and tm != gm:
        return 0.2
    return 0.0


def analyse(exe, audio):
    out = subprocess.run([exe, "--analyze", audio], capture_output=True, text=True, timeout=600).stdout
    shown = re.search(r"^shown key\s+(.+)$", out, re.M)
    nuvo = re.search(r"^nuvo key\s+(\S+ \S+)", out, re.M)
    return (parse_key(shown.group(1)) if shown else None), (parse_key(nuvo.group(1)) if nuvo else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dataset")
    ap.add_argument("exe", nargs="?", default=os.path.join("build", "ModeMatchTests_artefacts", "Release",
                                                           "ModeMatchTests.exe" if os.name == "nt" else "ModeMatchTests"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--csv")
    args = ap.parse_args()

    audio_dir = os.path.join(args.dataset, "audio")
    key_dir = os.path.join(args.dataset, "annotations", "key")
    tracks = []
    for name in sorted(os.listdir(audio_dir)):
        if not name.endswith(".mp3"):
            continue
        stem = name[:-4]
        key_file = os.path.join(key_dir, stem + ".key")
        if not os.path.exists(key_file):
            continue
        with open(key_file, encoding="utf-8") as f:
            truth = parse_key(f.read())
        if truth is not None:
            tracks.append((stem, os.path.join(audio_dir, name), truth))
    if not tracks:
        sys.exit("no annotated audio found under " + args.dataset)

    rows, shown_total, nuvo_total = [], 0.0, 0.0
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(analyse, args.exe, path): (stem, truth) for stem, path, truth in tracks}
        for done, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            stem, truth = futures[fut]
            shown, nuvo = fut.result()
            s, n = mirex(truth, shown), mirex(truth, nuvo)
            shown_total += s
            nuvo_total += n
            counts[s] = counts.get(s, 0) + 1
            rows.append((stem, truth, shown, nuvo, s, n))
            print(f"\r{done}/{len(tracks)}", end="", file=sys.stderr)
    print(file=sys.stderr)

    total = len(rows)
    names = {1.0: "correct", 0.5: "fifth", 0.3: "relative", 0.2: "parallel", 0.0: "other"}
    print(f"GiantSteps Key: {total} tracks")
    print(f"  Mode Match shown key  MIREX weighted score {100.0 * shown_total / total:.1f} %")
    print(f"  NuvoDeck profile key  MIREX weighted score {100.0 * nuvo_total / total:.1f} %")
    print("  shown key: " + ", ".join(f"{names[k]} {counts.get(k, 0)}" for k in (1.0, 0.5, 0.3, 0.2, 0.0)))
    if args.csv:
        fmt = lambda k: "" if k is None else f"{k[0]}{'m' if k[1] else ''}"
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["track", "truth", "shown", "nuvo", "score_shown", "score_nuvo"])
            for stem, truth, shown, nuvo, s, n in sorted(rows):
                w.writerow([stem, fmt(truth), fmt(shown), fmt(nuvo), s, n])


if __name__ == "__main__":
    main()
