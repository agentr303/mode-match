# Mode Match

Realtime pitch spiral + whole-song key (NuvoDeck's key detector, streaming) with modes and Camelot codes.
Design and decisions: [PLAN.md](PLAN.md).

## Build

Needs CMake 3.24+ and a C++20 compiler. JUCE 9.0.1 is taken from `C:/Dev/NuvoDeck/build/_deps/juce-src` when it
exists, otherwise downloaded (or pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>`).

### Windows (VST3 + Standalone)

```
cmake -S . -B build
cmake --build build --config Release --target ModeMatch_VST3 ModeMatch_Standalone
```

- VST3: `build/ModeMatch_artefacts/Release/VST3/Mode Match.vst3` - copy the whole `.vst3` folder to
  `C:\Program Files\Common Files\VST3\`.
- Standalone: `build/ModeMatch_artefacts/Release/Standalone/Mode Match.exe`

### macOS (AU + VST3 + Standalone)

AU can only be built on a Mac. Same CMake, with Xcode:

```
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target ModeMatch_AU ModeMatch_VST3 ModeMatch_Standalone
```

Copy `Mode Match.component` to `~/Library/Audio/Plug-Ins/Components/` and check it with `auval -v aufx Mdm1 Mdmh`.
For distribution, sign and notarise both bundles.

## Tests

```
cmake -S . -B build-tests -DRK_BUILD_PLUGIN=OFF
cmake --build build-tests --config Release --target ModeMatchTests
build-tests/ModeMatchTests_artefacts/Release/ModeMatchTests
```

Covers: NuvoDeck's 240-loop key test against both the copied `KeyDetector` and the streaming port (must agree), long
audio across 30 s blocks, loops replacing (not double counting) the timeline map, stopped transport, state round trip,
7 modes x 12 tonics, no false modes on major/minor material, Camelot codes, spiral peak and YIN accuracy.
Also: a minor key whose root is only in the bass (under KeyDetector's 100 Hz band) is shown by its bass/chord tonic,
and real recordings listed in `BassTonicTests` are checked when the file is in the project folder and `ffmpeg` is on
PATH (skipped otherwise; the songs are not shipped).

`ModeMatchTests --analyze <audio file>` prints the analysis of one recording (shown key, profile match, mode-stage
tonic scores, chroma, bass and chord roots); it decodes with `ffmpeg`.

`ModeMatchRender <dir>` (target `ModeMatchRender`, in the plugin build) runs the real processor and editor on
synthesised music and writes PNGs of the Analyze, Tune and compact layouts.

## Name

The product was renamed from "Radial Key" (trademark risk - see PLAN.md section 9). Clear "Mode Match" before a public release. To rename again, change `MM_PRODUCT_NAME`,
`MM_COMPANY_NAME`, `BUNDLE_ID` and the plugin codes (`Mdmh` / `Mdm1`) in `CMakeLists.txt`.
