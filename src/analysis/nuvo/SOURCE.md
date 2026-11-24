# Copied from NuvoDeck

- `KeyDetector.h`, `MusicalKey.h`: `C:\Dev\NuvoDeck\src\audio\utilities\` at NuvoDeck commit
  `f83cfed85c0fc085a6c80461a6b87086b4e2a639` (2026-11-20). Unchanged.
- `third_party/signalsmith-linear/fft.h`: NuvoDeck `third_party/signalsmith/signalsmith-linear/fft.h`, same commit.
  NuvoDeck's copy carries no licence file; upstream (Signalsmith Audio, signalsmith-linear) is MIT. Add its
  LICENSE file here before release.

`KeyAnalyzer` (src/analysis/KeyAnalyzer.*) is the streaming port: its frame step is KeyDetector::computeChroma's
per-frame code line for line; the tests compare it against KeyDetector::detectKey.
Plan: move both projects to one shared library later (PLAN.md 4a).
