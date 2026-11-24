#pragma once

#include <array>
#include <vector>
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

// Chords: root + 12 * quality. Saved states from 0.1.0 hold only major (0) and minor (1), which keep their numbers.
enum class ChordQuality { major = 0, minor, dominant7, diminished, sus4 };
inline constexpr int numChordQualities = 5;
inline int chordIndex(int root, ChordQuality q) { return root < 0 ? -1 : root % 12 + 12 * static_cast<int>(q); }
inline int chordRoot(int chord) { return chord < 0 ? -1 : chord % 12; }
inline ChordQuality chordQuality(int chord) { return static_cast<ChordQuality>(juce::jlimit(0, numChordQualities - 1, chord / 12)); }
// Major-sounding (major, 7) or minor-sounding (minor, diminished) for the tonic's quality; sus4 is neither.
inline bool chordIsMajorType(int chord) { const auto q = chordQuality(chord); return chord >= 0 && (q == ChordQuality::major || q == ChordQuality::dominant7); }
inline bool chordIsMinorType(int chord) { const auto q = chordQuality(chord); return chord >= 0 && (q == ChordQuality::minor || q == ChordQuality::diminished); }
inline juce::String chordName(int chord)
{
    if (chord < 0)
        return "-";
    static const char* suffix[] = { "", "m", "7", "dim", "sus4" };
    return noteName(chordRoot(chord)) + suffix[static_cast<int>(chordQuality(chord))];
}

// Steps between two keys on the Camelot wheel (0 = same number, i.e. the key or its relative; up to 6).
inline int camelotDistance(int a, int b)
{
    if (a < 0 || b < 0)
        return 6;
    const int d = std::abs(toMusicalKey(a).camelotNumber() - toMusicalKey(b).camelotNumber()) % 12;
    return std::min(d, 12 - d);
}

// Online Viterbi smoothing (PLAN.md 4a: "a 24-state HMM, high self-transition, cost by Camelot-wheel distance").
// Log domain: each step every state adds its emission score; leaving a state costs switchCost(from, to). The state
// shown is the best path's end now, so a new state takes over once its evidence has outweighed the switch cost.
class OnlineViterbi
{
public:
    void reset(int states) { score.assign(static_cast<std::size_t>(states), 0.0); started = false; best = -1; }
    template <typename Emission, typename SwitchCost>
    int step(Emission&& emission, SwitchCost&& switchCost)
    {
        const int n = static_cast<int>(score.size());
        next.resize(score.size());
        for (int s = 0; s < n; ++s)
        {
            double b = score[static_cast<std::size_t>(s)];
            if (started)
                for (int from = 0; from < n; ++from)
                    if (from != s)
                        b = std::max(b, score[static_cast<std::size_t>(from)] - switchCost(from, s));
            next[static_cast<std::size_t>(s)] = b + emission(s);
        }
        double top = -1.0e300;
        best = 0;
        for (int s = 0; s < n; ++s)
            if (next[static_cast<std::size_t>(s)] > top)
            {
                top = next[static_cast<std::size_t>(s)];
                best = s;
            }
        for (int s = 0; s < n; ++s)
            score[static_cast<std::size_t>(s)] = juce::jmax(next[static_cast<std::size_t>(s)] - top, -1.0e6); // keep it bounded
        started = true;
        return best;
    }
    [[nodiscard]] int state() const { return best; }

private:
    std::vector<double> score, next;
    bool started = false;
    int best = -1;
};

// The scale to tune to (PLAN.md 2, "Scale for tuning"): a mode, or major / natural / harmonic / melodic minor.
struct TuningScale
{
    int tonic = -1;
    juce::String type;        // "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Dorian", ...
    std::array<int, 7> notes {};
    [[nodiscard]] bool valid() const { return tonic >= 0; }
    [[nodiscard]] juce::String name() const { return valid() ? noteName(tonic) + " " + type : juce::String("-"); }
    // The notes spelled one letter each from the tonic's letter (A harmonic minor has G#, not Ab).
    [[nodiscard]] juce::StringArray spelledNotes() const
    {
        static const int natural[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const char letters[] = "CDEFGAB";
        const char first = noteName(tonic).toRawUTF8()[0];
        int letter = 0;
        while (letters[letter] != first)
            ++letter;
        juce::StringArray out;
        for (int i = 0; i < 7; ++i)
        {
            const int l = (letter + i) % 7;
            int diff = (notes[static_cast<std::size_t>(i)] - natural[l] + 12) % 12;
            if (diff > 6)
                diff -= 12;
            static const char* accidental[] = { "bb", "b", "", "#", "##" };
            out.add(juce::String::charToString(static_cast<juce::juce_wchar>(letters[l])) + accidental[juce::jlimit(-2, 2, diff) + 2]);
        }
        return out;
    }
    // Text for Melodyne / Auto-Tune: the scale name as both list it, then the notes.
    [[nodiscard]] juce::String clipboardText() const { return name() + " (" + spelledNotes().joinIntoString(" ") + ")"; }
};

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
