#include "KeyAnalyzer.h"

#include <climits>

namespace rk
{
// =============================================================================================================
// KeyFrameAnalyzer: NuvoDeck KeyDetector::computeChroma, the body of its per-frame loop.
// =============================================================================================================
KeyFrameAnalyzer::KeyFrameAnalyzer()
    : freq(frameSize / 2 + 1), windowed(frameSize), window(frameSize), logMagnitude(frameSize / 2 + 1)
{
    for (int i = 0; i < frameSize; ++i)
        window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * juce::MathConstants<double>::pi * i / frameSize)));
}

bool KeyFrameAnalyzer::analyse(const float* frame, double sampleRate, std::vector<Peak>& out, std::vector<Peak>* lowPeaks)
{
    constexpr double minFreq = 100.0;
    constexpr double maxFreq = 5000.0; // above it: cymbals and noise, and the harmonics below already count
    const int firstBin = juce::jmax(3, static_cast<int>(std::floor(minFreq * frameSize / sampleRate)));
    const int lastBin = juce::jmin(frameSize / 2 - 3, static_cast<int>(std::ceil(maxFreq * frameSize / sampleRate)));

    for (int i = 0; i < frameSize; ++i)
        windowed[static_cast<std::size_t>(i)] = static_cast<float>(static_cast<double>(frame[i]) * window[static_cast<std::size_t>(i)]);

    fft.fft(windowed.data(), freq.data());
    double peakPower = 0.0;
    for (int bin = firstBin - 3; bin <= lastBin + 3; ++bin)
    {
        const double power = std::norm(std::complex<double>(freq[static_cast<std::size_t>(bin)]));
        if (bin >= firstBin && bin <= lastBin)
            peakPower = std::max(peakPower, power);
        logMagnitude[static_cast<std::size_t>(bin)] = 0.5 * std::log(power + 1.0e-30);
    }
    if (peakPower <= 1.0e-12)
        return false; // silence
    const double floorPower = peakPower * 1.0e-5; // 50 dB under the loudest

    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const double a = logMagnitude[static_cast<std::size_t>(bin - 1)], b = logMagnitude[static_cast<std::size_t>(bin)],
                     c = logMagnitude[static_cast<std::size_t>(bin + 1)];
        if (! (b > a && b >= c))
            continue;
        if (b < logMagnitude[static_cast<std::size_t>(bin - 2)] || b < logMagnitude[static_cast<std::size_t>(bin - 3)]
            || b < logMagnitude[static_cast<std::size_t>(bin + 2)] || b < logMagnitude[static_cast<std::size_t>(bin + 3)])
            continue; // a side lobe of a louder peak
        const double power = std::exp(2.0 * b);
        if (power < floorPower)
            continue; // not a note: under the floor
        const double denominator = a - 2.0 * b + c;
        const double offset = denominator < 0.0 ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0.0;
        const double hz = (bin + offset) * sampleRate / static_cast<double>(frameSize);
        out.push_back({ 69.0 + 12.0 * std::log2(hz / 440.0), std::log1p(power / floorPower) });
    }

    // Mode Match's addition: the same peak rules from 35 Hz up to KeyDetector's first bin, into a separate list.
    if (lowPeaks != nullptr)
    {
        const int lowFirst = juce::jmax(3, static_cast<int>(std::floor(35.0 * frameSize / sampleRate)));
        for (int bin = lowFirst - 3; bin < firstBin - 3; ++bin)
            logMagnitude[static_cast<std::size_t>(bin)] = 0.5 * std::log(std::norm(std::complex<double>(freq[static_cast<std::size_t>(bin)])) + 1.0e-30);
        for (int bin = lowFirst; bin < firstBin; ++bin)
        {
            const double a = logMagnitude[static_cast<std::size_t>(bin - 1)], b = logMagnitude[static_cast<std::size_t>(bin)],
                         c = logMagnitude[static_cast<std::size_t>(bin + 1)];
            if (! (b > a && b >= c) || b < logMagnitude[static_cast<std::size_t>(bin - 2)] || b < logMagnitude[static_cast<std::size_t>(bin - 3)]
                || b < logMagnitude[static_cast<std::size_t>(bin + 2)] || b < logMagnitude[static_cast<std::size_t>(bin + 3)])
                continue;
            const double power = std::exp(2.0 * b);
            if (power < floorPower)
                continue;
            const double denominator = a - 2.0 * b + c;
            const double offset = denominator < 0.0 ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0.0;
            const double hz = (bin + offset) * sampleRate / static_cast<double>(frameSize);
            lowPeaks->push_back({ 69.0 + 12.0 * std::log2(hz / 440.0), std::log1p(power / floorPower) });
        }
    }
    return true;
}

// =============================================================================================================
// Helpers
// =============================================================================================================
namespace
{
constexpr int ringSize = KeyFrameAnalyzer::frameSize * 2; // a power of two
constexpr juce::int64 ringMask = ringSize - 1;
constexpr double bassMidiLimit = 59.2; // ~250 Hz

int fineIndex(double midi)
{
    double m = std::fmod(midi, 12.0);
    if (m < 0.0)
        m += 12.0;
    return static_cast<int>(std::floor(m * 10.0 + 0.5)) % TimelineBin::fineBins;
}

double tuneOffsetOf(double re, double im)
{
    return (re * re + im * im) > 1.0e-20 ? std::atan2(im, re) / (2.0 * juce::MathConstants<double>::pi) : 0.0; // -0.5 .. 0.5
}

void foldPeak(double midi, double weight, double tuneOffset, Chroma& out)
{
    const double mValue = midi - tuneOffset;
    const double mDown = std::floor(mValue);
    int pc = static_cast<int>(std::fmod(mDown, 12.0));
    if (pc < 0)
        pc += 12;
    const double fracUp = mValue - mDown;
    out[static_cast<std::size_t>(pc)] += weight * (1.0 - fracUp);
    out[static_cast<std::size_t>((pc + 1) % 12)] += weight * fracUp;
}

// The chord over the last half second. `bass` (the same frames below ~250 Hz, down to 35 Hz) speaks for the root:
// KeyDetector's chroma starts at 100 Hz, so a low root (F2 is 87 Hz) is weak in `c`. A root counts as present as the
// louder of its chroma and its bass share (scaled to the loudest pitch class), plus a small bonus for being the bass
// note, so Ab-C-Eb over an F bass reads as F minor (Fm7), not Ab major.
// No other mode named: when the mode stage's tonic is clear (bass and chord roots agree on it), that tonic wins over the
// profile match. Major or minor for it: the quality of the chords heard on the tonic (`tonicMajor` / `tonicMinor`, time
// on each), else the profiles for that tonic. Neither the stage's own scale nor the profiles alone are reliable there:
// on a flat chroma Ab and A weigh the same, and a 15 s window's scale wanders (F Dorian, F Mixolydian) while the tonic
// holds. Otherwise the profile match stands.
int majorMinorKeyFor(const ModeResult& mode, double tonicMajor, double tonicMinor, const Chroma& chroma, int profileKey)
{
    if (mode.modal || mode.tonic < 0 || mode.margin < ModeRules {}.minMargin)
        return profileKey;
    const int major = keyIndex(mode.tonic, false), minor = keyIndex(mode.tonic, true);
    if (tonicMajor + tonicMinor > 0.0 && tonicMajor != tonicMinor)
        return tonicMinor > tonicMajor ? minor : major;
    const auto scores = keyScores(chroma);
    return scores[static_cast<std::size_t>(minor)] >= scores[static_cast<std::size_t>(major)] ? minor : major;
}

// Time on major / minor chords rooted on `tonic` (chord = root + 12 * quality, 0 major, 1 minor).
template <typename Chords>
std::pair<double, double> tonicChordQualities(const Chords& chords, int tonic)
{
    double major = 0.0, minor = 0.0;
    for (const int c : chords)
        if (c >= 0 && tonic >= 0 && c % 12 == tonic)
            (c >= 12 ? minor : major) += 1.0;
    return { major, minor };
}

int bestChord(const Chroma& c, const Chroma& bass)
{
    double total = 0.0, cMax = 0.0, bassMax = 0.0;
    for (auto v : c)
    {
        total += v;
        cMax = std::max(cMax, v);
    }
    for (auto v : bass)
        bassMax = std::max(bassMax, v);
    if (total <= 0.0)
        return -1;
    constexpr double bassRootBonus = 0.25; // in units of the loudest pitch class
    int best = -1;
    double bestScore = -1e9;
    for (int root = 0; root < 12; ++root)
        for (int q = 0; q < 2; ++q)
        {
            const int third = (root + (q == 0 ? 4 : 3)) % 12, fifth = (root + 7) % 12;
            const double bassShare = bassMax > 0.0 ? cMax * bass[static_cast<std::size_t>(root)] / bassMax : 0.0;
            const double rootPresence = std::max(c[static_cast<std::size_t>(root)], bassShare);
            const double s = rootPresence * 1.1 + c[static_cast<std::size_t>(third)] + c[static_cast<std::size_t>(fifth)]
                             - 0.25 * total / 4.0 + bassRootBonus * bassShare;
            if (s > bestScore)
            {
                bestScore = s;
                best = root + 12 * q;
            }
        }
    return best;
}
} // namespace

void foldFine(const std::array<float, TimelineBin::fineBins>& fine, double tuneOffset, Chroma& out)
{
    for (int i = 0; i < TimelineBin::fineBins; ++i)
    {
        const double w = fine[static_cast<std::size_t>(i)];
        if (w > 0.0)
            foldPeak(i / 10.0, w, tuneOffset, out);
    }
}

// =============================================================================================================
// KeyAnalyzer
// =============================================================================================================
void KeyAnalyzer::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    ring.assign(static_cast<std::size_t>(ringSize), 0.0f);
    frameBuffer.assign(static_cast<std::size_t>(KeyFrameAnalyzer::frameSize), 0.0f);
    peaks.reserve(512);
    samplesIn = 0;
    nextFrameStart = 0;
    markers.clear();
    recentFrames.clear();
    recentBass.clear();
    recentChords.clear();
    freeRunSeconds = 0.0;
    lastBin = INT_MIN;
    wasPlaying = false;
}

void KeyAnalyzer::clear()
{
    timeline.clear();
    ++pass;
    lastBin = INT_MIN;
    smoothedLocalKey = rawLocalKey = candidateKey = -1;
    candidateRun = 0;
    chordNow = -1;
    recentFrames.clear();
    recentBass.clear();
    recentChords.clear();
}

int KeyAnalyzer::binIndexFor(double beat) const
{
    return static_cast<int>(std::floor(beat + 1.0e-9));
}

void KeyAnalyzer::beginPassIfJumped(int bin, bool playing)
{
    // A new pass starts when playback starts or the playhead jumps (a loop, a scrub): the bins it then reaches are
    // replaced, not added to.
    if (playing && ! wasPlaying)
        ++pass;
    else if (lastBin != INT_MIN && bin != lastBin && bin != lastBin + 1)
        ++pass;
    lastBin = bin;
    wasPlaying = playing;
}

void KeyAnalyzer::process(const float* mono, int numSamples, const HostPosition& position, double energy, int numChannels, bool accumulate)
{
    if (numSamples <= 0)
        return;
    HostPosition p = position;
    hostTimeline = p.hasTimeline;
    if (! p.hasTimeline)
    {
        // No timeline (standalone, live input): a running clock, one bin per half second. The clock stops in silence
        // (under -80 dBFS), so an idle input does not grow the map.
        const double meanSquare = energy / (static_cast<double>(numSamples) * juce::jmax(1, numChannels));
        p.playing = meanSquare > 1.0e-8;
        p.seconds = freeRunSeconds;
        p.bpm = 60.0 / freeRunBinSeconds;
        p.ppq = freeRunSeconds / freeRunBinSeconds;
        if (p.playing)
            freeRunSeconds += numSamples / sampleRate;
    }
    const bool acc = accumulate && p.playing;
    const int bin = binIndexFor(p.ppq);
    beginPassIfJumped(bin, p.playing);
    lastBeat = p.ppq;

    if (acc)
    {
        auto& b = timeline[bin];
        if (b.pass < pass)
        {
            b = TimelineBin {};
            b.pass = pass;
            b.seconds = p.seconds;
        }
        b.energy += energy;
        b.energyCount += static_cast<double>(numSamples) * juce::jmax(1, numChannels);
        b.samples += numSamples;
    }

    markers.push_back({ samplesIn, p, pass, acc });

    // Append in pieces of at most one hop, analysing each frame as soon as it is complete.
    int done = 0;
    while (done < numSamples)
    {
        const int piece = juce::jmin(numSamples - done, KeyFrameAnalyzer::hopSize);
        for (int i = 0; i < piece; ++i)
            ring[static_cast<std::size_t>((samplesIn + i) & ringMask)] = mono[done + i];
        samplesIn += piece;
        done += piece;
        while (samplesIn >= nextFrameStart + KeyFrameAnalyzer::frameSize)
        {
            analyseFrame(nextFrameStart, KeyFrameAnalyzer::frameSize);
            nextFrameStart += KeyFrameAnalyzer::hopSize;
        }
    }

    while (markers.size() > 1 && markers[1].sample <= samplesIn - ringSize)
        markers.pop_front();
}

void KeyAnalyzer::finish()
{
    if (samplesIn > 0 && nextFrameStart == 0 && samplesIn < KeyFrameAnalyzer::frameSize)
    {
        analyseFrame(0, static_cast<int>(samplesIn));
        nextFrameStart = KeyFrameAnalyzer::hopSize;
    }
}

const KeyAnalyzer::Marker* KeyAnalyzer::markerFor(juce::int64 sample) const
{
    const Marker* found = markers.empty() ? nullptr : &markers.front();
    for (const auto& m : markers)
        if (m.sample <= sample)
            found = &m;
        else
            break;
    return found;
}

double KeyAnalyzer::beatAt(const Marker& m, juce::int64 sample) const
{
    return m.position.ppq + static_cast<double>(sample - m.sample) / sampleRate * m.position.bpm / 60.0;
}

double KeyAnalyzer::secondsAt(const Marker& m, juce::int64 sample) const
{
    return m.position.seconds + static_cast<double>(sample - m.sample) / sampleRate;
}

void KeyAnalyzer::analyseFrame(juce::int64 frameStart, int validSamples)
{
    for (int i = 0; i < KeyFrameAnalyzer::frameSize; ++i)
        frameBuffer[static_cast<std::size_t>(i)] = i < validSamples ? ring[static_cast<std::size_t>((frameStart + i) & ringMask)] : 0.0f;

    peaks.clear();
    lowPeaks.clear();
    if (! frameAnalyzer.analyse(frameBuffer.data(), sampleRate, peaks, &lowPeaks) || peaks.empty())
        return;

    // Local key and chord from recent frames, folded with a running tuning estimate.
    double fre = 0.0, fim = 0.0;
    for (const auto& pk : peaks)
    {
        const double angle = 2.0 * juce::MathConstants<double>::pi * (pk.midi - std::round(pk.midi));
        fre += pk.weight * std::cos(angle);
        fim += pk.weight * std::sin(angle);
    }
    runRe = runRe * 0.97 + fre;
    runIm = runIm * 0.97 + fim;
    const double runOffset = tuneOffsetOf(runRe, runIm);
    // The chord and local-key path re-weights the peaks against a 30 dB floor (KeyDetector's is 50 dB, kept for the
    // song-key chroma). In a full mix ~200 peaks a frame clear 50 dB, so the chroma flattens and a chord's third (Ab vs A)
    // is decided by noise; at 30 dB the chord tones stand out.
    const auto strong = [](double w)
    {
        constexpr double keyFloor = 1.0e-5, chordFloor = 1.0e-3; // 50 dB and 30 dB under the frame's loudest peak
        const double relative = std::expm1(w) * keyFloor;        // undo log1p(power / (loudest * keyFloor))
        return relative >= chordFloor ? std::log1p(relative / chordFloor) : 0.0;
    };
    Chroma frameChroma {};
    Chroma frameBass {};
    for (const auto& pk : peaks)
    {
        foldPeak(pk.midi, strong(pk.weight), runOffset, frameChroma);
        if (pk.midi < bassMidiLimit)
            foldPeak(pk.midi, strong(pk.weight), runOffset, frameBass);
    }
    for (const auto& pk : lowPeaks)
        foldPeak(pk.midi, strong(pk.weight), runOffset, frameBass);
    updateLocalKeyAndChord(frameChroma, frameBass);

    // File the peaks in the timeline bin under the frame's centre.
    const juce::int64 centre = frameStart + juce::jmin(validSamples, KeyFrameAnalyzer::frameSize) / 2;
    const Marker* m = markerFor(centre);
    if (m == nullptr || ! m->accumulate)
        return;
    const int bin = binIndexFor(beatAt(*m, centre));
    auto& b = timeline[bin];
    if (m->pass > b.pass)
    {
        b = TimelineBin {};
        b.pass = m->pass;
        b.seconds = secondsAt(*m, centre);
    }
    else if (m->pass < b.pass)
        return; // a frame from before a jump reaching a bin the new pass already replaced
    for (const auto& pk : peaks)
    {
        const auto fi = static_cast<std::size_t>(fineIndex(pk.midi));
        b.fine[fi] += static_cast<float>(pk.weight);
        if (pk.midi < bassMidiLimit)
            b.bass[fi] += static_cast<float>(pk.weight);
        b.weight += static_cast<float>(pk.weight);

        // For the mode stage only: leave out peaks that are the 3rd, 5th, 6th or 7th harmonic of a stronger lower peak
        // (an E's 3rd harmonic is a B, which would hide Mixolydian's missing major 7th). Octave harmonics keep their
        // pitch class, so they stay.
        if (! isOvertone(pk))
            b.fund[fi] += static_cast<float>(pk.weight);
    }
    // Bass fundamentals below KeyDetector's band: bass chroma and fundamentals only (never the KeyDetector chroma).
    for (const auto& pk : lowPeaks)
    {
        const auto fi = static_cast<std::size_t>(fineIndex(pk.midi));
        b.bass[fi] += static_cast<float>(pk.weight);
        b.fund[fi] += static_cast<float>(pk.weight);
    }
    b.re += fre;
    b.im += fim;
    b.localKey = smoothedLocalKey;
    b.chord = chordNow;
}

bool KeyAnalyzer::isOvertone(const Peak& pk) const
{
    // The 3rd, 5th, 6th or 7th harmonic of a stronger lower peak (from either band). 0.07 semitone: tight enough that an
    // equal-tempered major third two octaves up (28.00) is not taken for a 5th harmonic (27.86).
    static constexpr double harmonicOffsets[] = { 19.020, 27.863, 31.020, 33.688 };
    const auto check = [&pk](const std::vector<Peak>& list)
    {
        for (const auto& lower : list)
        {
            if (lower.weight < pk.weight)
                continue;
            for (const double offset : harmonicOffsets)
                if (std::abs((pk.midi - lower.midi) - offset) < 0.07)
                    return true;
        }
        return false;
    };
    return check(peaks) || check(lowPeaks);
}

void KeyAnalyzer::updateLocalKeyAndChord(const Chroma& frameChroma, const Chroma& frameBass)
{
    const auto framesPerSecond = sampleRate / KeyFrameAnalyzer::hopSize;
    const auto window = static_cast<std::size_t>(juce::jmax(4.0, 15.0 * framesPerSecond)); // 15 s
    const int holdFrames = juce::jmax(2, static_cast<int>(4.0 * framesPerSecond));          // a new key must hold 4 s
    const std::size_t chordFrames = static_cast<std::size_t>(juce::jmax(2.0, 0.5 * framesPerSecond));

    recentFrames.push_back(frameChroma);
    while (recentFrames.size() > window)
        recentFrames.pop_front();
    recentBass.push_back(frameBass);
    while (recentBass.size() > window)
        recentBass.pop_front();
    Chroma recentBassSum {}, windowBass {};
    for (std::size_t i = 0; i < recentBass.size(); ++i)
        for (int k = 0; k < 12; ++k)
        {
            windowBass[static_cast<std::size_t>(k)] += recentBass[i][static_cast<std::size_t>(k)];
            if (i + chordFrames >= recentBass.size())
                recentBassSum[static_cast<std::size_t>(k)] += recentBass[i][static_cast<std::size_t>(k)];
        }

    Chroma sum {}, recent {};
    for (std::size_t i = 0; i < recentFrames.size(); ++i)
        for (int k = 0; k < 12; ++k)
        {
            sum[static_cast<std::size_t>(k)] += recentFrames[i][static_cast<std::size_t>(k)];
            if (i + chordFrames >= recentFrames.size())
                recent[static_cast<std::size_t>(k)] += recentFrames[i][static_cast<std::size_t>(k)];
        }
    chordNow = bestChord(recent, recentBassSum);
    recentChords.push_back(chordNow);
    while (recentChords.size() > window)
        recentChords.pop_front();

    // The same tonic rule as the song key (computeSummary): the profile match over the last 15 s, unless the bass and
    // chord roots of those 15 s name a plain major/minor tonic by a clear margin.
    Chroma roots {}, share {};
    for (const int c : recentChords)
        if (c >= 0)
            roots[static_cast<std::size_t>(c % 12)] += 1.0;
    double sumTotal = 0.0;
    for (auto v : sum)
        sumTotal += v;
    if (sumTotal > 0.0)
        for (int k = 0; k < 12; ++k)
            share[static_cast<std::size_t>(k)] = sum[static_cast<std::size_t>(k)] / sumTotal;
    const int profileKey = bestTwoKeys(keyScores(sum)).first;
    const auto localMode = detectMode({ share, share, windowBass, roots, profileKey });
    const auto [tonicMajor, tonicMinor] = tonicChordQualities(recentChords, localMode.tonic);
    rawLocalKey = majorMinorKeyFor(localMode, tonicMajor, tonicMinor, sum, profileKey);

    if (smoothedLocalKey < 0)
        smoothedLocalKey = rawLocalKey;
    else if (rawLocalKey == smoothedLocalKey)
    {
        candidateKey = -1;
        candidateRun = 0;
    }
    else
    {
        if (rawLocalKey == candidateKey)
            ++candidateRun;
        else
        {
            candidateKey = rawLocalKey;
            candidateRun = 1;
        }
        if (candidateRun >= holdFrames)
        {
            smoothedLocalKey = candidateKey;
            candidateKey = -1;
            candidateRun = 0;
        }
    }
}

KeySummary KeyAnalyzer::computeSummary() const
{
    KeySummary s;
    struct Block
    {
        double re = 0, im = 0, energy = 0, count = 0, samples = 0;
        std::vector<const TimelineBin*> bins;
    };
    std::map<int, Block> blocks;
    double totalRe = 0.0, totalIm = 0.0;
    int prevKey = -1;
    for (const auto& [index, b] : timeline)
    {
        juce::ignoreUnused(index);
        if (b.weight <= 0.0f && b.energyCount <= 0.0)
            continue;
        auto& block = blocks[static_cast<int>(std::floor(b.seconds / 30.0))];
        block.re += b.re;
        block.im += b.im;
        block.energy += b.energy;
        block.count += b.energyCount;
        block.samples += b.samples;
        block.bins.push_back(&b);
        totalRe += b.re;
        totalIm += b.im;
        if (b.weight > 0.0f)
            ++s.analysedBins;
        if (b.localKey >= 0)
        {
            if (prevKey >= 0 && b.localKey != prevKey)
                ++s.keyChanges;
            prevKey = b.localKey;
        }
    }
    if (s.analysedBins == 0)
        return s;

    // NuvoDeck's aggregation: per-block tuning and chroma, normalised, loudness^2-weighted.
    struct BlockChroma
    {
        Chroma c;
        double rms, length;
    };
    std::vector<BlockChroma> blockChromas;
    double maxRms = 0.0;
    for (const auto& [bi, block] : blocks)
    {
        juce::ignoreUnused(bi);
        const double offset = tuneOffsetOf(block.re, block.im);
        Chroma c {};
        for (const auto* b : block.bins)
            foldFine(b->fine, offset, c);
        double blockMax = 0.0;
        for (auto v : c)
            blockMax = std::max(blockMax, v);
        if (blockMax <= 0.0)
            continue;
        for (auto& v : c)
            v /= blockMax;
        const double rms = block.count > 0.0 ? std::sqrt(block.energy / block.count) : 0.0;
        maxRms = std::max(maxRms, rms);
        blockChromas.push_back({ c, rms, juce::jmin(1.0, block.samples / (sampleRate * 30.0)) });
    }
    if (blockChromas.empty())
        return s;

    Chroma aggregate {};
    if (blockChromas.size() == 1)
        aggregate = blockChromas.front().c;
    else
        for (const auto& bc : blockChromas)
        {
            const double weight = (maxRms > 0.0 ? std::pow(bc.rms / maxRms, 2.0) : 1.0) * bc.length;
            for (int i = 0; i < 12; ++i)
                aggregate[static_cast<std::size_t>(i)] += bc.c[static_cast<std::size_t>(i)] * weight;
        }
    double aggMax = 0.0;
    for (auto v : aggregate)
        aggMax = std::max(aggMax, v);
    if (aggMax <= 0.0)
        return s;
    for (auto& v : aggregate)
        v /= aggMax;
    s.aggregate = aggregate;

    const auto scores = keyScores(aggregate);
    const auto [best, second] = bestTwoKeys(scores);
    s.key = best;
    s.runnerUp = second;
    s.r = scores[static_cast<std::size_t>(best)];
    s.r2 = scores[static_cast<std::size_t>(second)];

    // Whole-song presence, bass and chord roots with the global tuning.
    const double globalOffset = tuneOffsetOf(totalRe, totalIm);
    s.tuningCents = globalOffset * 100.0;
    Chroma presence {}, bass {}, roots {}, fundamentals {};
    for (const auto& [index, b] : timeline)
    {
        juce::ignoreUnused(index);
        foldFine(b.fine, globalOffset, presence);
        foldFine(b.bass, globalOffset, bass);
        foldFine(b.fund, globalOffset, fundamentals);
        if (b.chord >= 0 && b.weight > 0.0f)
            roots[static_cast<std::size_t>(b.chord % 12)] += 1.0;
    }
    double total = 0.0;
    for (auto v : presence)
        total += v;
    if (total > 0.0)
        for (auto& v : presence)
            v /= total;
    s.presence = presence;

    // The mode stage reads the fundamentals (overtones left out) for the collection and the characteristic notes.
    double fundTotal = 0.0;
    for (auto v : fundamentals)
        fundTotal += v;
    Chroma fundShare {};
    if (fundTotal > 0.0)
        for (int i = 0; i < 12; ++i)
            fundShare[static_cast<std::size_t>(i)] = fundamentals[static_cast<std::size_t>(i)] / fundTotal;
    s.bass = bass;
    s.chordRoots = roots;
    s.mode = detectMode({ fundShare, fundShare, bass, roots, s.key });

    std::vector<int> binChords;
    binChords.reserve(timeline.size());
    for (const auto& [index, b] : timeline)
    {
        juce::ignoreUnused(index);
        if (b.weight > 0.0f)
            binChords.push_back(b.chord);
    }
    const auto [tonicMajor, tonicMinor] = tonicChordQualities(binChords, s.mode.tonic);
    s.majorMinorKey = majorMinorKeyFor(s.mode, tonicMajor, tonicMinor, aggregate, s.key);
    s.valid = true;
    return s;
}

// =============================================================================================================
// State
// =============================================================================================================
void KeyAnalyzer::writeTo(juce::OutputStream& out) const
{
    out.writeInt(0x524b5431); // "RKT1"
    out.writeInt(static_cast<int>(timeline.size()));
    for (const auto& [index, b] : timeline)
    {
        out.writeInt(index);
        out.write(b.fine.data(), sizeof(float) * b.fine.size());
        out.write(b.bass.data(), sizeof(float) * b.bass.size());
        out.write(b.fund.data(), sizeof(float) * b.fund.size());
        for (double v : { b.re, b.im, b.energy, b.energyCount, b.samples, b.seconds })
            out.writeDouble(v);
        out.writeInt(b.localKey);
        out.writeInt(b.chord);
        out.writeFloat(b.weight);
    }
}

bool KeyAnalyzer::readFrom(juce::InputStream& in)
{
    if (in.readInt() != 0x524b5431)
        return false;
    const int count = in.readInt();
    if (count < 0 || count > 1000000)
        return false;
    std::map<int, TimelineBin> loaded;
    for (int i = 0; i < count && ! in.isExhausted(); ++i)
    {
        const int index = in.readInt();
        TimelineBin b;
        in.read(b.fine.data(), static_cast<int>(sizeof(float) * b.fine.size()));
        in.read(b.bass.data(), static_cast<int>(sizeof(float) * b.bass.size()));
        in.read(b.fund.data(), static_cast<int>(sizeof(float) * b.fund.size()));
        b.re = in.readDouble();
        b.im = in.readDouble();
        b.energy = in.readDouble();
        b.energyCount = in.readDouble();
        b.samples = in.readDouble();
        b.seconds = in.readDouble();
        b.localKey = in.readInt();
        b.chord = in.readInt();
        b.weight = in.readFloat();
        b.pass = 0;
        loaded[index] = b;
    }
    timeline = std::move(loaded);
    pass = 1; // the next pass over these bins replaces them
    lastBin = INT_MIN;
    return true;
}
} // namespace rk
