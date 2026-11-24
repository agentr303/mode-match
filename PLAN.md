# Mode Match — plugin plan

A VST3 / AU / AAX / CLAP plugin (plus standalone) that shows **what is sounding right now** on a pitch spiral and **what key the whole piece is in** in one window. The spiral is drawn on top of the summative key, so every live note shows at a glance whether it is inside the song's scale.

Interactive mockup: see the published artifact (mockup + condensed plan).

---

## 1. What each reference contributes

| Area | From The Snail (realtime, per partial) | From Mixed In Key Studio Edition (summative) | Mode Match |
|---|---|---|---|
| Core view | Spiral, one turn per octave, C (Center Note) at top, blobs per partial | Root key readout, key changes, note-presence % | Spiral whose 12 sectors are shaded by the summative scale; blobs on top |
| Pitch precision | Phase-based frequency refinement, cents-level, harmonics & inharmonicity | Not shown | Per-partial cents offset, tuning-drift ring for the dominant pitch |
| Color meaning | Coherency index / magnitude / phase modes | Not shown | Hue = pitch class (circle-of-fifths order); red halo = outside summative scale |
| Time context | Sonogram (secondary view) | "See key changes inside your audio" | Key + chord timeline lane locked to the host timeline |
| Statistics | None | % of the song using each note; root key | Note presence %, key + Camelot code, confidence (Pearson r), runner-up key, measured tuning offset. Key engine from NuvoDeck |
| Workflow hooks | Tuner mode, target note, Click-and-Tune, transposing instrument | Key+Scale for Melodyne/Auto-Tune, use with Captain plugins | Copy scale for Melodyne/Auto-Tune; optional MIDI/scale output (phase 4) |
| Modes | Music / Tuner | One-button analysis | Listen (live) · Analyze (accumulate) · Lock (freeze summary) · Tune |

## 2. Feature spec (v1)

### Realtime spiral (left)
- One octave per turn, configurable center note, grid range (e.g. E1–C8), A4 reference 415–466 Hz.
- Blob per detected partial: radius ∝ loudness (perceptual, sone-ish), brightness ∝ stability; hue = pitch class mapped by fifths so related notes look related.
- Summative scale sectors: in-scale sectors softly lit, tonic sector strongest. Out-of-scale partials get a red halo.
- "Now" key shown as outer-rim ticks when it differs from the summative key (modulation shows up as rim and sector disagreeing).
- Center readout: current chord estimate, local key, tuning-drift ring (±50 cents) for the strongest fundamental. This is our own indicator, not the Snail's rotating hexagon.
- Tune mode: click a sector to set a target note, then a zoomed 7-semitone ruler appears (secondary view).

### Summative panel (right)
- **Key**: name + Camelot code (e.g. "A minor · 8A"), relative key, confidence (Pearson r), runner-up key with code. When a mode is detected: "D Dorian · 8A", a mode line naming the characteristic note and its %, and NuvoDeck's major/minor reading as runner-up. The spiral rings the characteristic note's label.
- **Note presence**: 12 bars, % of analyzed audio per pitch class (MIK SE-style), scale members highlighted.
- **Scale for tuning**: e.g. "A harmonic minor" when the raised 7th exceeds a presence threshold; copy button text formatted for Melodyne / Auto-Tune.
- **Camelot wheel**: 1–12 B outer / A inner with key names; song key filled, local key ringed, harmonic neighbours (±1, relative) lightly marked.
- **Measured tuning**: global reference estimate (e.g. "A4 = 438.6 Hz"), useful for old recordings and samples.

### Timeline (bottom)
- Key lane (segments colored by tonic, labeled at changes) + chord lane, with playhead.
- **Indexed by host timeline position (ppq/samples), not wall-clock.** Looping or scrubbing overwrites bins instead of double-counting, so the summary stays a true whole-song summary.
- Without transport (live input), falls back to a rolling capture buffer.

### Modes
- **Listen**: spiral only, summary paused.
- **Analyze**: accumulate into the timeline map (default).
- **Lock**: freeze the summary; the spiral keeps judging live notes against it. This is the main use for writing over a finished track or sample.
- **Tune**: tuner-style settings (narrow frequency tracking, target note).

## 3. Architecture

```
Audio thread (processBlock)
  └─ lock-free SPSC ring buffer (mono sum or selected channel)
Analysis thread (~100–200 Hz hop)
  ├─ Multi-resolution STFT (long windows for bass, short for treble)
  ├─ Magnitude peak picking + parabolic interpolation (NuvoDeck method, no phase); YIN for the tuner
  ├─ Peak picking + partial tracking  ──► Partial frames  ──► UI (spiral)
  ├─ NuvoDeck KeyDetector frame step (16384 FFT, peak picking, log weights)
  │     └─ Timeline map: 120-bin fine chroma + tuning sums per beat, keyed by host position
  ├─ Local key (8 s window, Temperley/Pearson) + HMM, chord templates ──► timeline lanes
  └─ Song key: NuvoDeck aggregation (30 s blocks, loudness², Temperley/Pearson) ──► summary + Camelot
UI thread (JUCE + OpenGL / visage), 60 fps, reads latest snapshot atomically
State: timeline map + summary saved in plugin state (project recall)
```

- Plugin is pass-through (zero latency); analysis never blocks audio.
- Optional **ARA 2** (Studio One, Cubase, Logic, Reaper, Cakewalk): read the whole clip offline and fill the summary without playing it through. This is how we offer MIK's "scan the whole file" result inside the DAW.

## 4. DSP approach

### 4a. Key detection: the NuvoDeck engine (`C:\Dev\NuvoDeck\src\audio\utilities\KeyDetector.h`, `MusicalKey.h`)

The summary key uses NuvoDeck's `KeyDetector`, with the same math. What it does today (offline, whole buffer):

1. **Chroma per frame**: 16384-point Hann FFT (signalsmith-linear), hop 4096, 100–5000 Hz. Only spectral peaks count: a peak must be the highest within ±3 bins and within 50 dB of the frame's loudest. Each peak is placed between bins by a parabola through the log magnitudes, and weighted `log1p(power / floor)`.
2. **Tuning**: circular mean of every peak's distance from the A440 grid (handles ±0.5 semitone wrap). Peaks are shifted by it, then soft-assigned across the two nearest pitch classes.
3. **Whole-track aggregation**: 30 s blocks. Each block's chroma is normalized to its max, weighted by `(blockRms / maxRms)^2` (the loud sections define the key), summed and normalized.
4. **Key**: Pearson correlation against **Temperley (Kostka-Payne) profiles** for 12 rotations × major/minor, then argmax. Confidence = the winning Pearson r.
5. **Camelot**: `MusicalKey::camelot()` (8B = C major, 8A = A minor, a step per fifth). `keyFromLabel()` parses "7A 124 Artist – Title" names.

**Streaming port for the plugin** (`src/analysis/KeyAnalyzer.{h,cpp}`):
- Split `computeChroma` into a per-frame step (`analyzeFrame` → peaks) and an aggregation step. The frame code stays line-for-line identical. It runs on the analysis thread as audio arrives, using a 16384-sample ring and a 4096 hop. That is 341 ms of context and 85 ms between updates at 48 kHz, which is fine for a summary. The spiral uses its own short-window engine.
- **Timeline map bin** (per beat, keyed by host ppq): a **120-bin fine chroma** (10-cent resolution) of peak weights, the tuning sums `Σw·cos`, `Σw·sin`, plus energy and sample count. The fine chroma lets us re-fold pitch classes after the global tuning is known, exactly like NuvoDeck's two-pass approach. This works without storing raw peaks (a 6-min song is about 800 bins × ~1 KB).
- **Summary** = NuvoDeck's aggregation over the map: group bins into 30 s blocks of host time, use loudness² weights, then Pearson against Temperley profiles. **One change:** NuvoDeck drops the trailing partial block, but the plugin keeps it (weighted by its length) so short loops and song endings count.
- **Runner-up key**: second-best of the same 24 correlations (NuvoDeck already computes them). It is shown with its Camelot code.
- **Local key and key changes** (an addition, NuvoDeck untouched): the same correlation over a sliding 8 s window of bins, smoothed with a 24-state HMM (high self-transition, cost by Camelot-wheel distance).
- **Names**: `MusicalKey::name()` spelling (Db, Eb, F#, Ab, Bb), displayed as "A minor · 8A".
- **Track-name hint (optional)**: VST3/CLAP give the host track name. If `keyFromLabel()` finds a Camelot label there (e.g. a sample named "8A 124 …"), show it beside the detected key as a cross-check, never overriding it.

### 4a-2. Mode detection (new stage on top of NuvoDeck; NuvoDeck stays untouched)

NuvoDeck answers only major or minor. A Dorian vamp (Dm7–G) usually comes back as "D minor" or "A minor", and a Mixolydian rock riff as its Ionian neighbour. A second stage decides the mode from the same aggregate chroma plus tonic evidence:

1. **Pitch collection.** Correlate the aggregate chroma with the 12 diatonic sets (binary 7-note templates), plus harmonic-minor and melodic-minor sets. The best fit gives the notes in use (e.g. the C-major collection).
2. **Tonic.** Score each of the 7 notes in the collection as the tonic:
   - bass-weighted chroma (energy below ~250 Hz)
   - chord-root time from the chord lane
   - notes at phrase ends and on downbeats (host bar position)
   - agreement with NuvoDeck's tonic
3. **Mode** = where the winning tonic sits in the collection:

   | Tonic on degree | Mode | Characteristic note checked |
   |---|---|---|
   | 1 | Ionian → reported as **major** | — |
   | 2 | **Dorian** | raised 6th (vs natural minor) |
   | 3 | **Phrygian** | flat 2nd |
   | 4 | **Lydian** | raised 4th |
   | 5 | **Mixolydian** | flat 7th (vs major) |
   | 6 | Aeolian → reported as **minor** | — |
   | 7 | **Locrian** | flat 2nd + flat 5th |

4. **Reporting rule.** A non-major/minor mode is shown only when:
   - its tonic score beats the next candidate by a set margin,
   - its characteristic note makes up at least 3 % of the audio, and
   - the note it replaces stays under 1 % (e.g. Dorian: B ≥ 3 %, Bb < 1 %).

   Otherwise NuvoDeck's major/minor result stands, with a small "modal hint: Dorian?" when the evidence is close.
5. **Display.** "D Dorian" plus the **Camelot code of the major/minor key with the same notes**. Minor-type modes (Dorian, Phrygian, Locrian) take the A-ring code, major-type modes (Lydian, Mixolydian) the B-ring code. So D Dorian shows **8A** (same notes as A minor / C major), because harmonic mixing depends on shared notes. NuvoDeck's own major/minor reading (e.g. "D minor · 7A") is shown as the runner-up, so results can be compared with other tools.
6. **Scale output.** Melodyne and Auto-Tune both offer the church modes, so the copy-scale text uses the mode name ("D Dorian").
7. **Tests.** Synthesized vamps for all 7 modes × 12 tonics, in the same style as the 240-loop test. Target: correct collection 100 %, correct mode ≥ 85 %, and Ionian/Aeolian songs never reported as another mode (no false positives on the existing 240 loops).

**Code sharing.** v1: copy both headers into `src/analysis/nuvo/` with the frame/aggregate split, and record the NuvoDeck commit they came from. Later: extract a small shared `nevo-key` static library that both projects link, so fixes flow both ways.

**Tests carried over:**
- Port `KeyDetectionUnitTests`: 240 synthesized loops (12 tonics × 2 modes × 5 tunings −45..+45 cents × 2 sample rates). Pass criteria: ≥ 95 % exact, 0 semitone errors, 0 far-off results.
- New **streaming-equivalence test**: run a file through `KeyAnalyzer` block by block, compare against `KeyDetector::detectKey` on the whole buffer, and require the same key (same chroma within 1e-6 apart from the trailing-block rule).
- New: loop/scrub test. Playing bars 1–8 three times must give the same summary as playing them once.

### 4b. Realtime spiral
- **Frequency precision: magnitude only, the same approach as NuvoDeck's KeyDetector.** Spectral peaks are picked from FFT magnitudes (highest within ±3 bins, above a dB floor), then placed between bins with a parabola through the log magnitudes. **No FFT phase is used anywhere.** For speed and bass resolution:
  - multi-resolution windows: 16384 below ~200 Hz, 8192 for mid, 4096 / 2048 for treble;
  - 2–4× zero-padding;
  - a bias-corrected interpolator for the Hann window.

  Expected precision: under 1 cent above ~200 Hz with 8192 + padding, a few cents in the bass.
- **Tuner fundamental (Tune mode)**: time-domain pitch estimation (YIN / normalized autocorrelation), also phase-free. It gives sub-cent readings on sustained notes.
- **Blob brightness ("stability")**: how steady a tracked peak's interpolated frequency and magnitude are across frames (variance over ~150 ms). It is not computed from phase.
- **Why phase-free:** The Snail's patent family ([EP 3155609 B1](https://data.epo.org/gpi/EP3155609B1), priority FR 1455456, CNRS / IRCAM, "Frequency analysis by phase demodulation of an acoustic signal") describes computing the demodulated phase, a stability coefficient of it, a second coefficient from the demodulated-phase difference between successive frames, and showing the result as a helical (spiral) display. A frame-to-frame phase-difference engine feeding a spiral would sit close to that, so we don't build one. Still get a freedom-to-operate opinion on the final design, since the spiral display itself appears in the patent's description. The patent was filed in 2014, so it runs to about 2034 if maintained; check which countries it is still in force in.
- **Chords**: template matching (maj, min, 7, dim, sus) over short-window chroma, with HMM smoothing. NuvoDeck's `chords::detectKey` (MIDI clips) is not used here, since the plugin works on audio.
- **Benchmarks**: on top of the ported unit tests, GiantSteps Key, scored with the MIREX weighted key score.

## 5. Tech stack
- C++20, **JUCE 9.0.1** (VST3, AU, AAX, Standalone) + clap-juce-extensions (CLAP). Built so far: VST3 + Standalone (Windows), AU from the same CMake on macOS.
- FFT: signalsmith-linear (the one NuvoDeck's KeyDetector already uses), for both engines.
- GPU UI: JUCE OpenGL context (or visage) for the spiral; CPU fallback ("Reduce GPU usage").
- ARA SDK 2.x for offline analysis.
- Tests: Catch2 for DSP units; pluginval in CI; Python scripts for dataset evaluation.
- CI: GitHub Actions, Windows + macOS universal builds, code signing / notarization.

## 6. Milestones

1. **Port the NuvoDeck key engine (1–2 wks)**: copy `KeyDetector.h` / `MusicalKey.h`, split into frame + aggregate steps, timeline map, port the 240-loop test, add the streaming-equivalence and loop/scrub tests.
2. **Engine (2–3 wks)**: ring buffer, host-position binning, local key + HMM, realtime partial tracker for the spiral; all headless with unit tests.
3. **Plugin shell + spiral (3 wks)**: JUCE project, GPU spiral, partial blobs, scale-sector overlay.
4. **Summative panel + timeline (3 wks)**: host-position timeline map, note presence, key lane, chord lane, Lock mode, state save/recall.
5. **Tune mode + workflow (2 wks)**: target note, zoomed ruler, transposing instruments, copy-scale, presets.
6. **ARA 2 + MIDI out (3 wks)**: offline whole-clip analysis; optional MIDI key-switch / scale output for downstream plugins.
7. **Beta (4 wks)**: pluginval, DAW matrix (Ableton, Logic, FL, Cubase, Pro Tools, Reaper, Bitwig, Studio One), performance tuning, docs.

## 7. Risks
- **IP**: Snail's analysis is patented; MIK's algorithm is proprietary. Key detection is our own (NuvoDeck). The spiral uses the same magnitude-only peak method, so no FFT phase is used anywhere. "Camelot" is a Mixed In Key trademark: we show the codes (as NuvoDeck does) but don't use the name in branding, and the docs credit the trademark.
- **NuvoDeck drift**: the copied engine can diverge from NuvoDeck. Record the source commit and move to the shared `nevo-key` library once both projects are stable.
- **Accuracy on dense mixes**: drums and bass smear chroma. Mitigate with HPSS, bass weighting, and confidence display so users know when to distrust it.
- **CPU/GPU**: many instances on many tracks. Shared analysis thread pool, sleep when the editor is closed (keep accumulating at low rate), GPU-off fallback.
- **Host timeline quirks**: some hosts give no ppq when stopped or in clip-launch mode, so fall back to wall-clock capture.

## 8. Decisions (2026-11-21)
- **Key engine**: NuvoDeck `KeyDetector` (Temperley profiles, Pearson, loudness² 30 s blocks), ported to streaming.
- **Scope of the summary**: each instance keeps its own song key (saved in its plugin state). No cross-instance sharing.
- **Notation**: standard names + Camelot codes everywhere ("A minor · 8A"), Camelot wheel in the summary panel.
- **Licensing**: none for now. No activation, no network access.
- **Modes**: Dorian, Phrygian, Lydian, Mixolydian and Locrian are named when detected. Ionian/Aeolian are shown as major/minor. The Camelot code is the one with the same notes.

## 9. IP & licensing checklist (not legal advice; have an IP attorney confirm before commercial release)

| Item | Status / action |
|---|---|
| Product name | "Radial Key" dropped: Radial Engineering Ltd. is an established pro-audio brand. **Now "Mode Match"**: run a trademark clearance search (classes 9 and 42) before release. |
| The Snail patent FR 1455456 / EP 3155609 B1 (CNRS, licensed to IRCAM) | The claims centre on **demodulated FFT phase** and its frame-to-frame stability. Our engines use magnitudes only (NuvoDeck's method) plus time-domain pitch for the tuner, so they stay outside that core. Remaining question for counsel: whether showing a spiral display of spectral peaks alone touches any claim. Check status per country (filed 2014 → ~2034). |
| The Snail look | Pitch spirals are old public-domain ideas (Drobisch 1855, Shepard). Keep our own visual language: no rotating hexagon, no "milky moustache", no copied colors or layout. |
| Mixed In Key | No code or assets copied. Features (key, note %, key changes) are ideas, which copyright doesn't protect. Run a quick patent search on key-change and realtime key display. |
| "Camelot" | MIK trademark. Codes are used descriptively (as NuvoDeck already does). Keep "Camelot" out of the product name and marketing headlines, and add a trademark credit in the docs/about box. |
| NuvoDeck code | Confirm you own it outright (no employer/client claim, no third-party code inside apart from listed libraries). |
| Key profiles (Temperley, Krumhansl) | Published research data. Fine to use; cite in docs. |
| JUCE 8 | Use a licence that matches revenue (Starter/Indie/Pro) or AGPLv3. Closed-source sale needs a commercial JUCE licence once over the free tier. |
| VST3 SDK | MIT since VST3 SDK 3.8 (the copy bundled with JUCE 9). The VST logo/name still needs Steinberg's usage guidelines. |
| AAX | Avid developer agreement + PACE signing. |
| ARA 2 SDK | Apache 2.0, keep the notice. |
| CLAP / clap-juce-extensions | MIT, keep the notices. |
| signalsmith-linear | NuvoDeck's vendored copy (`third_party/signalsmith/`) has **no licence file**. Upstream is MIT (verify on its repo), so add the LICENSE file to both projects and include the notice in the about box. |
| Fonts | Mockup fonts are SIL OFL; fine to embed with the licence text if used in the plugin. |
