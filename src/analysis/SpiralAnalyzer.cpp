#include "SpiralAnalyzer.h"

namespace rk
{
namespace
{
constexpr int ringSize = 32768; // power of two >= longSize
constexpr juce::int64 ringMask = ringSize - 1;
constexpr double floorDb = 60.0;
constexpr std::size_t maxPartials = 72;

std::vector<float> hann(int n)
{
    std::vector<float> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        w[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * juce::MathConstants<double>::pi * i / n)));
    return w;
}
} // namespace

void SpiralAnalyzer::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    hop = juce::jmax(256, static_cast<int>(sampleRate / 50.0));
    ring.assign(ringSize, 0.0f);
    longWindow = hann(longSize);
    midWindow = hann(midSize);
    shortWindow = hann(shortSize);
    tinyWindow = hann(tinySize);
    frame.assign(longSize, 0.0f);
    spectrum.assign(longSize / 2 + 1, {});
    logMag.assign(longSize / 2 + 1, 0.0);
    current.clear();
    previous.clear();
    drift.clear();
    written = sinceUpdate = 0;
}

bool SpiralAnalyzer::push(const float* mono, int numSamples)
{
    bool updated = false;
    for (int i = 0; i < numSamples; ++i)
    {
        ring[static_cast<std::size_t>(written & ringMask)] = mono[i];
        ++written;
        if (++sinceUpdate >= hop && written >= shortSize)
        {
            sinceUpdate = 0;
            analyse();
            updated = true;
        }
    }
    return updated;
}

void SpiralAnalyzer::spectrumPeaks(int frameSize, int fftSize, signalsmith::linear::Pow2RealFFT<float>& fftEngine, const std::vector<float>& window,
                                   double minHz, double maxHz, std::vector<std::pair<double, double>>& out)
{
    // The newest frameSize samples, windowed, zero-padded to fftSize.
    for (int i = 0; i < fftSize; ++i)
    {
        float v = 0.0f;
        if (i < frameSize)
        {
            const juce::int64 s = written - frameSize + i;
            v = s >= 0 ? ring[static_cast<std::size_t>(s & ringMask)] * window[static_cast<std::size_t>(i)] : 0.0f;
        }
        frame[static_cast<std::size_t>(i)] = v;
    }
    fftEngine.fft(frame.data(), spectrum.data());

    const double binHz = sampleRate / fftSize;
    const int firstBin = juce::jmax(3, static_cast<int>(std::floor(minHz / binHz)));
    const int lastBin = juce::jmin(fftSize / 2 - 4, static_cast<int>(std::ceil(maxHz / binHz)));
    // Window gain and zero-padding change the scale; normalise power to a full-scale sine of this frame size.
    const double norm = 4.0 / (static_cast<double>(frameSize) * frameSize * 0.25);
    for (int bin = firstBin - 3; bin <= lastBin + 3; ++bin)
        logMag[static_cast<std::size_t>(bin)] = 0.5 * std::log(std::norm(std::complex<double>(spectrum[static_cast<std::size_t>(bin)])) * norm + 1.0e-30);

    // Peaks as in KeyDetector: the highest within +-3 bins, interpolated by a parabola through the log magnitudes.
    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const double a = logMag[static_cast<std::size_t>(bin - 1)], b = logMag[static_cast<std::size_t>(bin)], c = logMag[static_cast<std::size_t>(bin + 1)];
        if (! (b > a && b >= c))
            continue;
        if (b < logMag[static_cast<std::size_t>(bin - 2)] || b < logMag[static_cast<std::size_t>(bin - 3)]
            || b < logMag[static_cast<std::size_t>(bin + 2)] || b < logMag[static_cast<std::size_t>(bin + 3)])
            continue;
        const double denominator = a - 2.0 * b + c;
        const double offset = denominator < 0.0 ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0.0;
        const double peakLog = b - 0.25 * (a - c) * offset; // the parabola's height at its vertex
        out.emplace_back((bin + offset) * binHz, std::exp(2.0 * peakLog));
    }
}

void SpiralAnalyzer::analyse()
{
    // Input level of the newest short frame.
    double sum = 0.0;
    for (int i = 0; i < shortSize; ++i)
    {
        const float v = ring[static_cast<std::size_t>((written - shortSize + i) & ringMask)];
        sum += static_cast<double>(v) * v;
    }
    rmsNow = static_cast<float>(std::sqrt(sum / shortSize));

    std::vector<std::pair<double, double>> raw;
    raw.reserve(256);
    if (written >= longSize)
        spectrumPeaks(longSize, longSize, longFftEngine, longWindow, 35.0, 200.0, raw);
    if (written >= midSize)
        spectrumPeaks(midSize, longSize, longFftEngine, midWindow, 200.0, 700.0, raw);
    spectrumPeaks(shortSize, longSize, longFftEngine, shortWindow, 700.0, 2000.0, raw);
    spectrumPeaks(tinySize, tinyFft, tinyFftEngine, tinyWindow, 2000.0, 4200.0, raw);

    double maxPower = 0.0;
    for (const auto& r : raw)
        maxPower = std::max(maxPower, r.second);

    previous.swap(current);
    std::vector<float> previousDrift;
    previousDrift.swap(drift);
    current.clear();
    if (maxPower > 1.0e-10) // about -100 dBFS: silence otherwise
    {
        const double maxDb = 10.0 * std::log10(maxPower);
        for (const auto& [hz, power] : raw)
        {
            const double db = 10.0 * std::log10(power + 1e-30);
            const double rel = db - maxDb;
            if (rel < -floorDb)
                continue;
            // Level: position above the floor, also dimmed when the whole signal is quiet.
            const double absolute = juce::jlimit(0.0, 1.0, (db + 70.0) / 70.0);
            const double level = std::pow((rel + floorDb) / floorDb, 1.6) * (0.35 + 0.65 * absolute);
            current.push_back({ static_cast<float>(69.0 + 12.0 * std::log2(hz / referenceA4)), static_cast<float>(level), 0.0f });
        }
        std::sort(current.begin(), current.end(), [](const Partial& x, const Partial& y) { return x.level > y.level; });
        if (current.size() > maxPartials)
            current.resize(maxPartials);
    }

    // Stability: match each partial to the nearest one of the previous update and smooth the change.
    drift.assign(current.size(), 0.0f);
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        float bestDistance = 0.35f;
        int match = -1;
        for (std::size_t j = 0; j < previous.size(); ++j)
        {
            const float d = std::abs(current[i].midi - previous[j].midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                match = static_cast<int>(j);
            }
        }
        const float prevDrift = match >= 0 && static_cast<std::size_t>(match) < previousDrift.size() ? previousDrift[static_cast<std::size_t>(match)] : 0.2f;
        const float change = match >= 0 ? bestDistance : 0.35f;
        drift[i] = prevDrift * 0.8f + change * 0.2f;
        current[i].stability = std::exp(-drift[i] * 40.0f);
    }

    detectPitch();
}

void SpiralAnalyzer::detectPitch()
{
    // YIN, in two steps: a coarse search on a ~12 kHz decimated copy (cheap), then the full-rate difference function
    // around the coarse lag, interpolated by a parabola. Range ~40 Hz .. 1.6 kHz.
    pitchNow = {};
    if (rmsNow < 1.0e-3 || written < 8192)
        return;
    const int factor = juce::jmax(1, static_cast<int>(std::round(sampleRate / 12000.0)));
    const double rate = sampleRate / factor;
    const int window = static_cast<int>(rate * 0.06);            // 60 ms integration
    const int maxLag = static_cast<int>(rate / 40.0), minLag = juce::jmax(2, static_cast<int>(rate / 1600.0));
    const int needed = window + maxLag + 2;
    if (static_cast<juce::int64>(needed) * factor > written)
        return;
    decimated.assign(static_cast<std::size_t>(needed), 0.0f);
    const juce::int64 start = written - static_cast<juce::int64>(needed) * factor;
    for (int i = 0; i < needed; ++i)
    {
        float acc = 0.0f;
        for (int k = 0; k < factor; ++k)
            acc += ring[static_cast<std::size_t>((start + i * factor + k) & ringMask)];
        decimated[static_cast<std::size_t>(i)] = acc / static_cast<float>(factor);
    }
    yinDiff.assign(static_cast<std::size_t>(maxLag + 1), 0.0f);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double d = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double x = decimated[static_cast<std::size_t>(j)] - decimated[static_cast<std::size_t>(j + lag)];
            d += x * x;
        }
        yinDiff[static_cast<std::size_t>(lag)] = static_cast<float>(d);
    }
    // Cumulative mean normalised difference.
    double running = 0.0;
    int lagFound = -1;
    float bestValue = 1.0f;
    std::vector<float> cmnd(static_cast<std::size_t>(maxLag + 1), 1.0f);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += yinDiff[static_cast<std::size_t>(lag)];
        cmnd[static_cast<std::size_t>(lag)] = running > 0.0 ? static_cast<float>(yinDiff[static_cast<std::size_t>(lag)] * lag / running) : 1.0f;
    }
    for (int lag = minLag; lag < maxLag; ++lag)
        if (cmnd[static_cast<std::size_t>(lag)] < 0.15f)
        {
            while (lag + 1 < maxLag && cmnd[static_cast<std::size_t>(lag + 1)] < cmnd[static_cast<std::size_t>(lag)])
                ++lag;
            lagFound = lag;
            bestValue = cmnd[static_cast<std::size_t>(lag)];
            break;
        }
    if (lagFound < 0)
        return;

    // Refine at the full rate around lagFound * factor.
    const int fullWindow = static_cast<int>(sampleRate * 0.06);
    const int centre = lagFound * factor;
    const int from = juce::jmax(2, centre - factor - 1), to = centre + factor + 1;
    if (static_cast<juce::int64>(fullWindow + to + 2) > written)
        return;
    const juce::int64 fullStart = written - (fullWindow + to + 2);
    auto diffAt = [&](int lag)
    {
        double d = 0.0;
        for (int j = 0; j < fullWindow; ++j)
        {
            const double x = ring[static_cast<std::size_t>((fullStart + j) & ringMask)] - ring[static_cast<std::size_t>((fullStart + j + lag) & ringMask)];
            d += x * x;
        }
        return d;
    };
    int bestLag = from;
    double bestDiff = 1e300;
    std::vector<double> diffs(static_cast<std::size_t>(to - from + 1));
    for (int lag = from; lag <= to; ++lag)
    {
        diffs[static_cast<std::size_t>(lag - from)] = diffAt(lag);
        if (diffs[static_cast<std::size_t>(lag - from)] < bestDiff)
        {
            bestDiff = diffs[static_cast<std::size_t>(lag - from)];
            bestLag = lag;
        }
    }
    double refined = bestLag;
    if (bestLag > from && bestLag < to)
    {
        const double a = diffs[static_cast<std::size_t>(bestLag - from - 1)], b = diffs[static_cast<std::size_t>(bestLag - from)],
                     c = diffs[static_cast<std::size_t>(bestLag - from + 1)];
        const double den = a - 2.0 * b + c;
        if (den > 0.0)
            refined += juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / den);
    }
    const double hz = sampleRate / refined;
    pitchNow.valid = true;
    pitchNow.midi = static_cast<float>(69.0 + 12.0 * std::log2(hz / referenceA4));
    pitchNow.clarity = juce::jlimit(0.0f, 1.0f, 1.0f - bestValue);
}
} // namespace rk
