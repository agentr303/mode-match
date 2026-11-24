# Third-party notices

## Bundled in this repository

### signalsmith-linear (`third_party/signalsmith-linear/fft.h`)
Copyright (c) 2025 Signalsmith Audio. MIT License, full text in
[`third_party/signalsmith-linear/LICENSE.txt`](third_party/signalsmith-linear/LICENSE.txt).
Modified: see [`third_party/signalsmith-linear/README.md`](third_party/signalsmith-linear/README.md).

## Used at build time (not bundled)

### JUCE
Fetched by CMake, not included in this repository. JUCE is dual-licensed under AGPLv3 or a commercial JUCE licence
(https://juce.com/legal/juce-8-licence/). Mode Match is AGPLv3, so it uses JUCE under JUCE's AGPLv3 option; binaries built from this project are AGPLv3.

### VST3 SDK
Included with JUCE. MIT License (VST3 SDK 3.8+). "VST" is a trademark of Steinberg Media Technologies GmbH.

### ARA Audio Random Access SDK 2.3.0 (ARA API and ARA Library)
Fetched by CMake when `MM_ENABLE_ARA` is on (the default) and compiled into the plugin. Copyright Celemony Software
GmbH. Apache License 2.0 (https://www.apache.org/licenses/LICENSE-2.0); the licence text ships in the release
downloads as `ARA-LICENSE.txt`. ARA is a trademark of Celemony Software GmbH.

### CLAP and clap-helpers
Fetched with clap-juce-extensions when `MM_ENABLE_CLAP` is on (the default) and compiled into the CLAP plugin.
Copyright (c) 2021 Alexandre Bique. MIT License (in the release downloads as `CLAP-LICENSE.txt`).

### clap-juce-extensions
Fetched by CMake (https://github.com/free-audio/clap-juce-extensions); builds the CLAP version from the JUCE plugin.
Copyright 2019-2020 Paul Walker. MIT License (in the release downloads as `clap-juce-extensions-LICENSE.txt`).

## Research data

The key-profile weights come from published research:
- C. L. Krumhansl, *Cognitive Foundations of Musical Pitch* (Oxford University Press, 1990): Krumhansl-Kessler
  profiles and the Krumhansl-Schmuckler key-finding algorithm.
- D. Temperley, *The Cognition of Basic Musical Structures* (MIT Press, 2001) and *Music and Probability*
  (MIT Press, 2007): Temperley key profiles.

## Trademarks

"Camelot" is a trademark of Mixed In Key LLC. Mode Match shows Camelot-style key codes descriptively and is not
affiliated with or endorsed by Mixed In Key LLC. Other names are the trademarks of their respective owners.
