#pragma once

#include <juce_core/juce_core.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cmath>
#include <array>
#include <complex>
#include <vector>
#include "signalsmith-linear/fft.h"

class KeyDetector
{
public:
    struct KeyResult
    {
        int rootNote = 0;
        bool isMinor = false;
        bool found = true;
        juce::String keyName;
        juce::String camelotCode;
        double confidence = 0.0;

        juce::String toString() const
        {
            return keyName + " (" + camelotCode + ") - confidence: " + juce::String(confidence, 2);
        }
    };

    static constexpr const char* noteNamesSharp[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static constexpr const char* noteNamesFlat[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

    // Temperley's (Kostka-Payne) key profiles (S9.3, 2026-10-29; Krumhansl's before): the tonic, fifth and the notes
    // both modes share weigh the same in major and minor, so the third and the sixth decide the mode. With Krumhansl's,
    // a loud fifth (a drone, a bass line) pulled a minor loop to its parallel major.
    static constexpr double majorKeyProfile[12] = {
        5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0
    };

    static constexpr double minorKeyProfile[12] = {
        5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0
    };

    KeyResult detectKey(const float* const* audioData, int numSamples, int numChannels, double sampleRate)
    {
        // Analyse the ENTIRE track rather than one arbitrary 30 s slice:
        // the previous single-window approach drifted between the key of
        // the intro and the key of the drop depending on where the window
        // landed (verified on a real track that measures Am in some windows
        // and Dm in others). Consecutive ~30 s blocks are analysed and
        // averaged with equal weight, so the result reflects the tonal
        // centre of the whole composition.
        std::array<double, 12> aggregateChroma = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        bool haveChroma = false;

        const int windowLen = static_cast<int>(30.0 * sampleRate);
        if (numSamples <= 0)
        {
            KeyResult result;
            result.keyName = "Unknown";
            return result;
        }

        if (numSamples <= windowLen)
        {
            aggregateChroma = computeChroma(audioData, numSamples, numChannels, sampleRate, 0, numSamples);
            haveChroma = true;
        }
        else
        {
            // Gather each block's chroma and loudness, then combine with a
            // loudness bias: the "drop"/main energy section of a track is the
            // most musically defined and (in dance music) defines its key.
            std::vector<std::array<double, 12>> blockChromas;
            std::vector<double> blockRms;
            double maxRms = 0.0;
            for (int start = 0; start + windowLen <= numSamples; start += windowLen)
            {
                double energy = 0.0;
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int i = 0; i < windowLen; ++i)
                    {
                        const double v = audioData[ch][start + i];
                        energy += v * v;
                    }
                const double rms = std::sqrt(energy / (static_cast<double>(windowLen) * numChannels));
                maxRms = std::max(maxRms, rms);

                auto blockChroma = computeChroma(audioData, numSamples, numChannels, sampleRate, start, windowLen);
                double blockMax = 0.0;
                for (const auto v : blockChroma)
                    blockMax = std::max(blockMax, v);
                if (blockMax <= 0.0)
                    continue;
                for (auto& v : blockChroma)
                    v /= blockMax;
                blockChromas.push_back(blockChroma);
                blockRms.push_back(rms);
            }

            constexpr double loudnessExponent = 2.0;
            if (maxRms > 0.0)
            {
                for (std::size_t b = 0; b < blockChromas.size(); ++b)
                {
                    const double weight = std::pow(blockRms[b] / maxRms, loudnessExponent);
                    for (int i = 0; i < 12; ++i)
                        aggregateChroma[static_cast<std::size_t>(i)] += blockChromas[b][i] * weight;
                }
                haveChroma = true;
            }
        }

        if (haveChroma)
        {
            double aggregateMax = 0.0;
            for (const auto v : aggregateChroma)
                aggregateMax = std::max(aggregateMax, v);
            if (aggregateMax > 0.0)
                for (auto& v : aggregateChroma)
                    v /= aggregateMax;
        }
        else
        {
            KeyResult result;
            result.keyName = "Unknown";
            return result;
        }

        std::array<double, 12> majorCorr = correlateWithKeyProfile(aggregateChroma, majorKeyProfile);
        std::array<double, 12> minorCorr = correlateWithKeyProfile(aggregateChroma, minorKeyProfile);

        KeyResult result;
        double bestCorr = -1e10;

        for (int i = 0; i < 12; ++i)
        {
            if (majorCorr[i] > bestCorr)
            {
                bestCorr = majorCorr[i];
                result.rootNote = i;
                result.isMinor = false;
                result.confidence = majorCorr[i];
            }
            if (minorCorr[i] > bestCorr)
            {
                bestCorr = minorCorr[i];
                result.rootNote = i;
                result.isMinor = true;
                result.confidence = minorCorr[i];
            }
        }

        result.keyName = getKeyName(result.rootNote, result.isMinor);
        result.camelotCode = getCamelotCode(result.rootNote, result.isMinor);

        return result;
    }

    // Reads an encoded Camelot key from a file name in the Mixed-In-Key /
    // DJ-pool convention (e.g. "7A 121 Artist - Title.mp3", "12B - Track").
    // Many libraries name files this way, and the label encodes the intended
    // key more reliably than chroma analysis of a harmonically mobile track.
    // Returns found=false when no well-formed label is present.
    static KeyResult keyFromLabel(const juce::String& text)
    {
        KeyResult result;
        int rootNote = 0;
        bool isMinor = false;
        if (! parseCamelotLabel(text, rootNote, isMinor))
        {
            result.found = false;
            return result;
        }
        result.rootNote = rootNote;
        result.isMinor = isMinor;
        result.keyName = getKeyName(rootNote, isMinor);
        result.camelotCode = getCamelotCode(rootNote, isMinor);
        result.confidence = 1.0;
        return result;
    }

private:
    std::array<double, 12> computeChroma(const float* const* audioData, int totalSamples, int numChannels, double sampleRate, int startSample, int analyzeLength)
    {
        std::array<double, 12> chroma = {0};

        const int endSample = juce::jmin(startSample + analyzeLength, totalSamples);
        const int samplesToAnalyze = endSample - startSample;
        if (samplesToAnalyze <= 0)
            return chroma;

        // Chroma extraction via a short-time Fourier chromagram, the front end of the Krumhansl-Schmuckler key finder
        // (TEST_FEEDBACK_PLAN.md S9.3, 2026-10-29):
        // - a 16384-point frame (2.9 Hz bins at 48 kHz) from 100 Hz up, so a bin is narrower than a semitone wherever
        //   notes are counted;
        // - only spectral peaks count (the highest within 3 bins, so the window's side lobes don't, and within 50 dB of
        //   the frame's loudest), each placed between bins by a parabola through its log magnitudes;
        // - each peak's power is log-compressed (against that 50 dB floor), so one loud bass note doesn't outweigh the
        //   harmony;
        // - the tuning is the circular mean of every peak's distance from the A440 grid (the angle of the weighted sum
        //   of e^(i 2 pi frac)) over every frame: a straight average breaks near half a semitone, where +0.45 and
        //   -0.45 are the same tuning.
        // Each note is soft-assigned across its two nearest pitch classes; its harmonics land on their own classes.
        constexpr int frameSize = 16384;
        constexpr int hopSize = 4096;

        signalsmith::linear::Pow2RealFFT<float> fft(frameSize);
        std::vector<std::complex<float>> freq(frameSize / 2 + 1);
        std::vector<float> monoFrame(frameSize);
        std::vector<double> logMagnitude(frameSize / 2 + 1);

        std::vector<float> window(frameSize);
        for (int i = 0; i < frameSize; ++i)
            window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * M_PI * i / frameSize)));

        constexpr double minFreq = 100.0;
        constexpr double maxFreq = 5000.0; // above it: cymbals and noise, and the harmonics below already count
        const int firstBin = juce::jmax(3, static_cast<int>(std::floor(minFreq * frameSize / sampleRate)));
        const int lastBin = juce::jmin(frameSize / 2 - 3, static_cast<int>(std::ceil(maxFreq * frameSize / sampleRate)));
        // A short clip still gets one frame (zero-padded).
        const int frames = samplesToAnalyze >= frameSize ? 1 + (samplesToAnalyze - frameSize) / hopSize : 1;

        struct Peak { double midi; double weight; };
        std::vector<Peak> peaks;
        peaks.reserve(static_cast<std::size_t>(frames) * 64);

        for (int frameIdx = 0; frameIdx < frames; ++frameIdx)
        {
            const int frameStart = startSample + frameIdx * hopSize;
            for (int i = 0; i < frameSize; ++i)
            {
                double s = 0.0;
                if (frameStart + i < endSample)
                    for (int ch = 0; ch < numChannels; ++ch)
                        s += audioData[ch][frameStart + i];
                monoFrame[static_cast<std::size_t>(i)] = static_cast<float>((s / static_cast<double>(numChannels)) * window[static_cast<std::size_t>(i)]);
            }

            fft.fft(monoFrame.data(), freq.data());
            double peakPower = 0.0;
            for (int bin = firstBin - 3; bin <= lastBin + 3; ++bin)
            {
                const double power = std::norm(std::complex<double>(freq[static_cast<std::size_t>(bin)]));
                if (bin >= firstBin && bin <= lastBin)
                    peakPower = std::max(peakPower, power);
                logMagnitude[static_cast<std::size_t>(bin)] = 0.5 * std::log(power + 1.0e-30);
            }
            if (peakPower <= 1.0e-12)
                continue; // silence
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
                peaks.push_back({ 69.0 + 12.0 * std::log2(hz / 440.0), std::log1p(power / floorPower) });
            }
        }
        if (peaks.empty())
            return chroma;

        double re = 0.0, im = 0.0;
        for (const auto& p : peaks)
        {
            const double angle = 2.0 * M_PI * (p.midi - std::round(p.midi));
            re += p.weight * std::cos(angle);
            im += p.weight * std::sin(angle);
        }
        const double tuneOffset = (re * re + im * im) > 1.0e-20 ? std::atan2(im, re) / (2.0 * M_PI) : 0.0; // -0.5 .. 0.5

        for (const auto& p : peaks)
        {
            const double mValue = p.midi - tuneOffset;
            const double mDown = std::floor(mValue);
            int pc = static_cast<int>(std::fmod(mDown, 12.0));
            if (pc < 0)
                pc += 12;
            const double fracUp = mValue - mDown;
            chroma[static_cast<std::size_t>(pc)] += p.weight * (1.0 - fracUp);
            chroma[static_cast<std::size_t>((pc + 1) % 12)] += p.weight * fracUp;
        }

        // Normalise so the strongest pitch class is 1.0.
        double maxChroma = 0.0;
        for (int i = 0; i < 12; ++i)
            maxChroma = std::max(maxChroma, chroma[static_cast<std::size_t>(i)]);

        if (maxChroma > 0.0)
        {
            for (int i = 0; i < 12; ++i)
                chroma[static_cast<std::size_t>(i)] /= maxChroma;
        }

        return chroma;
    }

    std::array<double, 12> correlateWithKeyProfile(const std::array<double, 12>& chroma, const double* profile)
    {
        std::array<double, 12> correlation = {0};

        // Standard key-profile rotation: rotate the CHROMA (not the profile)
        // so the argmax shift is the tonic pitch class directly. Rotating the
        // profile instead would invert the index and misreport every root
        // except C (=0) as its (12 - root) relative.
        // Pearson's correlation, as Krumhansl-Schmuckler has it (S9.3): both means taken off, so the notes a key leaves
        // out count as much as the ones it has (a plain cosine let a strong tonic and fifth decide, and a flat-ish
        // chroma read minor as its parallel major).
        double chromaMean = 0.0, profileMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            chromaMean += chroma[static_cast<std::size_t>(i)] / 12.0;
            profileMean += profile[i] / 12.0;
        }
        for (int shift = 0; shift < 12; ++shift)
        {
            double corr = 0;
            double chromaVar = 0;
            double profileVar = 0;

            for (int i = 0; i < 12; ++i)
            {
                const int idx = (i + shift) % 12;
                const double c = chroma[static_cast<std::size_t>(idx)] - chromaMean, p = profile[i] - profileMean;
                corr += c * p;
                chromaVar += c * c;
                profileVar += p * p;
            }

            if (chromaVar > 1e-10 && profileVar > 1e-10)
                correlation[shift] = corr / std::sqrt(chromaVar * profileVar);
        }

        return correlation;
    }

    static juce::String getKeyName(int rootNote, bool isMinor)
    {
        const char* const* names = noteNamesSharp;
        juce::String quality = isMinor ? "m" : "";
        return juce::String(names[rootNote]) + quality;
    }

    // Parses a Camelot label like "7A", "12A", "8B", "3a" (case-insensitive)
    // when it appears at the start of a name. Returns false for anything
    // that is not a well-formed 1-12 + A/B combination.
    static bool parseCamelotLabel(const juce::String& text, int& rootNoteOut, bool& isMinorOut)
    {
        const juce::String t = text.trimStart();
        int idx = 0;
        while (idx < t.length() && t[idx] >= '0' && t[idx] <= '9')
            ++idx;
        if (idx == 0 || idx > 2)
            return false;
        const int num = t.substring(0, idx).getIntValue();
        if (num < 1 || num > 12)
            return false;
        if (idx >= t.length())
            return false;
        const juce::juce_wchar letter = t[idx];
        if (letter != 'A' && letter != 'a' && letter != 'B' && letter != 'b')
            return false;
        // The letter must not run into another word ("8Backup" != a key).
        if (idx + 1 < t.length())
        {
            const juce::juce_wchar after = t[idx + 1];
            const bool isWordChar = (after >= '0' && after <= '9')
                                    || (after >= 'A' && after <= 'Z')
                                    || (after >= 'a' && after <= 'z');
            if (isWordChar)
                return false;
        }

        const bool isMinor = (letter == 'A' || letter == 'a');

        // Invert the Camelot wheel: number -> pitch class of its major key.
        // number n maps to the pitch class r where (7r + 8) % 12 == n (% 12);
        // 7 is its own inverse mod 12.
        int rootNote = ((7 * ((num % 12) - 8)) % 12 + 12) % 12;
        if (isMinor)
            rootNote = (rootNote - 3 + 12) % 12; // relative minor of the major key

        rootNoteOut = rootNote;
        isMinorOut = isMinor;
        return true;
    }

    // Standard Mixed In Key Camelot Wheel. Each step moves a perfect
    // fifth (7 semitones): 8B=C, 9B=G, 10B=D, 11B=A, 12B=E, 1B=B,
    // 2B=F#, 3B=C#/Db, 4B=G#/Ab, 5B=D#/Eb, 6B=A#/Bb, 7B=F. The inner
    // ring holds the relative minors (+3 semitones): 8A=Am, 9A=Em,
    // 10A=Bm, 11A=F#m, 12A=C#m, 1A=G#m, 2A=D#m, 3A=A#m, 4A=Fm,
    // 5A=Cm, 6A=Gm, 7A=Dm.
    static int standardCamelotNumber(int rootNote)
    {
        int code = (7 * rootNote + 8) % 12;
        return code == 0 ? 12 : code;
    }

    static juce::String getCamelotCode(int rootNote, bool isMinor)
    {
        const int code = standardCamelotNumber((rootNote + (isMinor ? 3 : 0)) % 12);
        return juce::String(code) + (isMinor ? "A" : "B");
    }
};
