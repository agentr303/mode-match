#pragma once

#include <complex>
#include <vector>

#include <juce_core/juce_core.h>
#include "signalsmith-linear/fft.h"

namespace rk
{
// The spiral's realtime engine (PLAN.md 4b). Magnitude only, the same peak method as NuvoDeck's KeyDetector:
// spectral peaks from FFT magnitudes, placed between bins by a parabola through the log magnitudes. No FFT phase is
// used anywhere (that is what keeps it clear of the phase-demodulation patent, EP 3155609).
//
// Four resolutions per update (PLAN.md 4b), longer windows where notes are closer in Hz:
//   35-200 Hz     16384-point frame
//   200-700 Hz    8192, zero-padded 2x to 16384
//   700-2000 Hz   4096, zero-padded 4x to 16384
//   2000-4200 Hz  2048, zero-padded 4x to 8192
// Zero-padding keeps the log-parabola's bias on a Hann window small (well under 1 cent above 200 Hz). Updates ~50 times a
// second. Tune mode reads the fundamental from YIN (time domain).
struct Partial
{
    float midi = 0.0f;      // MIDI note number with cents (relative to the A4 reference)
    float level = 0.0f;     // 0..1, perceptual-ish (dB above a -60 dB floor)
    float stability = 0.0f; // 0..1, how steady its frequency has been over the last ~150 ms
};

struct PitchReading
{
    bool valid = false;
    float midi = 0.0f;      // fundamental, with cents
    float clarity = 0.0f;   // 0..1 (1 - YIN aperiodicity)
};

class SpiralAnalyzer
{
public:
    void prepare(double sampleRate);
    void setReferenceA4(double hz) { referenceA4 = juce::jlimit(400.0, 480.0, hz); }

    // Feed mono audio. Returns true when a new set of partials is ready.
    bool push(const float* mono, int numSamples);
    [[nodiscard]] const std::vector<Partial>& partials() const { return current; }
    [[nodiscard]] PitchReading pitch() const { return pitchNow; }
    [[nodiscard]] float inputLevel() const { return rmsNow; }

private:
    void analyse();
    void spectrumPeaks(int frameSize, int fftSize, signalsmith::linear::Pow2RealFFT<float>& fft, const std::vector<float>& window,
                       double minHz, double maxHz, std::vector<std::pair<double, double>>& out); // (hz, power)
    void detectPitch();

    double sampleRate = 48000.0, referenceA4 = 440.0;
    static constexpr int longSize = 16384, midSize = 8192, shortSize = 4096, tinySize = 2048, tinyFft = 8192;
    std::vector<float> ring;
    juce::int64 written = 0, sinceUpdate = 0;
    int hop = 960;

    signalsmith::linear::Pow2RealFFT<float> longFftEngine { longSize }, tinyFftEngine { tinyFft };
    std::vector<float> longWindow, midWindow, shortWindow, tinyWindow, frame;
    std::vector<std::complex<float>> spectrum;
    std::vector<double> logMag;

    std::vector<Partial> current, previous;
    std::vector<float> drift; // per current partial: smoothed |frequency change|
    PitchReading pitchNow;
    float rmsNow = 0.0f;
    std::vector<float> yinDiff, decimated;
};
} // namespace rk
