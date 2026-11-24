#pragma once

#include <array>
#include <juce_core/juce_core.h>
#include "nuvo/MusicalKey.h"

namespace rk
{
using Chroma = std::array<double, 12>;

// Key index used everywhere in Mode Match: 0..11 = C..B major, 12..23 = C..B minor, -1 = unknown.
inline int keyIndex(int tonic, bool minor) { return tonic < 0 ? -1 : (minor ? 12 : 0) + tonic; }
inline int keyTonic(int index) { return index % 12; }
inline bool keyIsMinor(int index) { return index >= 12; }
inline nevo::audio::MusicalKey toMusicalKey(int index)
{
    return index < 0 ? nevo::audio::MusicalKey {} : nevo::audio::MusicalKey { keyTonic(index), keyIsMinor(index) };
}

// Names follow MusicalKey::name() spelling (Db, Eb, F#, Ab, Bb).
inline juce::String noteName(int pc)
{
    static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}
inline juce::String keyShortName(int index) { return index < 0 ? juce::String("-") : toMusicalKey(index).name(); }
inline juce::String keyLongName(int index)
{
    return index < 0 ? juce::String("-") : noteName(keyTonic(index)) + (keyIsMinor(index) ? " minor" : " major");
}
inline juce::String camelotCode(int index) { return index < 0 ? juce::String() : toMusicalKey(index).camelot(); }
inline int relativeKey(int index) { return index < 0 ? -1 : (keyIsMinor(index) ? (keyTonic(index) + 3) % 12 : 12 + (keyTonic(index) + 9) % 12); }

enum class Mode { ionian = 0, dorian, phrygian, lydian, mixolydian, aeolian, locrian };

inline const char* modeName(Mode m)
{
    static const char* names[] = { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian" };
    return names[static_cast<int>(m)];
}
// Semitones of each mode's scale above its tonic.
inline std::array<int, 7> modeIntervals(Mode m)
{
    static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int start = major[static_cast<int>(m)];
    std::array<int, 7> out {};
    for (int i = 0; i < 7; ++i)
        out[static_cast<std::size_t>(i)] = (major[(i + static_cast<int>(m)) % 7] - start + 12) % 12;
    return out;
}
inline bool modeIsMinorType(Mode m) { return m == Mode::dorian || m == Mode::phrygian || m == Mode::aeolian || m == Mode::locrian; }

// Temperley (Kostka-Payne) profiles, exactly as NuvoDeck's KeyDetector.
inline constexpr double temperleyMajor[12] = { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
inline constexpr double temperleyMinor[12] = { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };

// NuvoDeck's correlateWithKeyProfile: Pearson, the chroma rotated so the shift is the tonic. 24 scores, key-indexed.
inline std::array<double, 24> keyScores(const Chroma& chroma)
{
    std::array<double, 24> out {};
    for (int mode = 0; mode < 2; ++mode)
    {
        const double* profile = mode == 0 ? temperleyMajor : temperleyMinor;
        double chromaMean = 0.0, profileMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            chromaMean += chroma[static_cast<std::size_t>(i)] / 12.0;
            profileMean += profile[i] / 12.0;
        }
        for (int shift = 0; shift < 12; ++shift)
        {
            double corr = 0, cv = 0, pv = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double c = chroma[static_cast<std::size_t>((i + shift) % 12)] - chromaMean, p = profile[i] - profileMean;
                corr += c * p;
                cv += c * c;
                pv += p * p;
            }
            out[static_cast<std::size_t>(mode * 12 + shift)] = (cv > 1e-10 && pv > 1e-10) ? corr / std::sqrt(cv * pv) : 0.0;
        }
    }
    return out;
}

// Best and runner-up key. KeyDetector scans tonics 0..11 testing major then minor with a strict '>', so ties go to the
// lower tonic, major first; the same order is kept here.
inline std::pair<int, int> bestTwoKeys(const std::array<double, 24>& s)
{
    int best = -1, second = -1;
    for (int t = 0; t < 12; ++t)
        for (int k : { t, 12 + t })
        {
            if (best < 0 || s[static_cast<std::size_t>(k)] > s[static_cast<std::size_t>(best)])
            {
                second = best;
                best = k;
            }
            else if (second < 0 || s[static_cast<std::size_t>(k)] > s[static_cast<std::size_t>(second)])
                second = k;
        }
    return { best, second };
}
} // namespace rk
