#pragma once

#include "KeyTypes.h"

namespace rk
{
// The mode stage (PLAN.md 4a-2). It runs after NuvoDeck's major/minor answer and decides whether the song is in
// another church mode, from the same chroma plus tonic evidence (bass and chord roots).
struct ModeResult
{
    bool modal = false;           // a non-major/minor mode passed the reporting rule
    bool hint = false;            // close, but not proven: show "modal hint"
    Mode mode = Mode::ionian;     // the stage's best mode (valid when modal or hint)
    int tonic = -1;               // its tonic pitch class
    int collection = -1;          // pitch class of the major key with the same notes
    int characteristicPc = -1;    // the note that makes the mode (e.g. B for D Dorian)
    int replacedPc = -1;          // the note it replaces in plain major/minor (Bb for D Dorian)
    double characteristicShare = 0.0, replacedShare = 0.0; // share of all analysed notes, 0..1
    double margin = 0.0;          // tonic score: best minus runner-up
    std::array<double, 7> tonicScores {}; // score of each collection degree as tonic (diagnostics)

    // The scale to show and copy: the mode's 7 pitch classes.
    [[nodiscard]] std::array<int, 7> scale() const
    {
        std::array<int, 7> out {};
        const auto iv = modeIntervals(mode);
        for (int i = 0; i < 7; ++i)
            out[static_cast<std::size_t>(i)] = (tonic + iv[static_cast<std::size_t>(i)]) % 12;
        return out;
    }
    // Camelot code of the major/minor key with the same notes: A ring for minor-type modes, B ring for major-type.
    [[nodiscard]] int camelotKeyIndex() const
    {
        if (collection < 0)
            return -1;
        return modeIsMinorType(mode) ? keyIndex((collection + 9) % 12, true) : keyIndex(collection, false);
    }
    [[nodiscard]] juce::String name() const { return noteName(tonic) + " " + modeName(mode); }
};

struct ModeInputs
{
    Chroma chroma {};       // aggregate chroma (any scale)
    Chroma presence {};     // share of each pitch class, sums to 1
    Chroma bass {};         // chroma of peaks below ~250 Hz
    Chroma chordRoots {};   // time spent on chords rooted on each pitch class
    Chroma phraseRoots {};  // chord roots on downbeats, phrase starts and phrase ends (host timeline only; may be empty)
    int nuvoKey = -1;       // NuvoDeck's major/minor answer (key index)
};

struct ModeRules
{
    double minMargin = 0.06;            // tonic score margin needed to name a mode
    double minCharacteristic = 0.03;    // characteristic note at least 3 % of the audio
    double maxReplaced = 0.01;          // the note it replaces under 1 %
};

ModeResult detectMode(const ModeInputs& in, const ModeRules& rules = {});
} // namespace rk
