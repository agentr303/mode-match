# signalsmith-linear (vendored, modified)

- Upstream: https://github.com/Signalsmith-Audio/linear (Signalsmith Audio), MIT licence: see `LICENSE.txt`.
- File: `fft.h` only, taken via NuvoDeck (`third_party/signalsmith/signalsmith-linear/fft.h`).
- **Modified:** the block at the end of upstream `fft.h` that includes the optional platform back-ends
  (`./platform/fft-*.h`, selected by `SIGNALSMITH_USE_PFFFT`, `_ACCELERATE`, `_IPP`, `_CMSISDSP`, `_XSIMD`) is removed,
  because those headers are not vendored. The rest of the file is unchanged from upstream.
