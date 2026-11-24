#pragma once

#include <juce_core/juce_core.h>

namespace nevo::audio
{
// A musical key: tonic pitch class (0 = C ... 11 = B) and mode. Reads Camelot codes ("8A", "12b") and
// key names ("Am", "C#m", "Eb", "F# major", "Bb minor"). Used by the key leader (Bible §4.14): the deck
// MATCH button and, later, the arp sequencer's scale.
struct MusicalKey
{
    int tonic = -1; // -1: unknown
    bool minor = false;

    [[nodiscard]] bool isValid() const { return tonic >= 0 && tonic < 12; }

    [[nodiscard]] static MusicalKey parse(const juce::String& keyString)
    {
        MusicalKey key;
        const auto text = keyString.trim();
        if (text.isEmpty())
            return key;

        // Camelot: majors are "B", minors "A" (8B = C major, 8A = A minor; each step is a fifth).
        const int number = text.getIntValue();
        const auto letter = text.trimCharactersAtStart("0123456789").trim().toUpperCase();
        if (number >= 1 && number <= 12 && (letter == "A" || letter == "B"))
        {
            key.minor = letter == "A";
            const int majorTonic = wrap(7 * (number - 8)); // 8B = C, 9B = G, 7B = F ...
            key.tonic = key.minor ? wrap(majorTonic - 3) : majorTonic;
            return key;
        }

        static constexpr int letterPitch[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
        const juce::juce_wchar first = juce::CharacterFunctions::toUpperCase(text[0]);
        if (first < 'A' || first > 'G')
            return key;
        int pitch = letterPitch[first - 'A'];
        auto rest = text.substring(1);
        if (rest.startsWithChar('#') || rest.startsWith(juce::CharPointer_UTF8("\xe2\x99\xaf")))
        {
            ++pitch;
            rest = rest.substring(1);
        }
        else if (rest.startsWithChar('b') || rest.startsWith(juce::CharPointer_UTF8("\xe2\x99\xad")))
        {
            --pitch;
            rest = rest.substring(1);
        }
        rest = rest.trim().toLowerCase();
        const bool isMinor = rest.startsWith("m") && ! rest.startsWith("maj");
        if (! (rest.isEmpty() || isMinor || rest.startsWith("maj")))
            return key;
        key.tonic = wrap(pitch);
        key.minor = isMinor;
        return key;
    }

    [[nodiscard]] MusicalKey transposed(int semitones) const
    {
        return isValid() ? MusicalKey { wrap(tonic + semitones), minor } : *this;
    }

    // Camelot number (1-12) and code ("8A").
    [[nodiscard]] int camelotNumber() const
    {
        const int majorTonic = minor ? wrap(tonic + 3) : tonic;
        return wrap(7 * majorTonic + 7) + 1; // C major = 8, a fifth up (G) = 9 ...
    }
    [[nodiscard]] juce::String camelot() const
    {
        return isValid() ? juce::String(camelotNumber()) + (minor ? "A" : "B") : juce::String();
    }
    [[nodiscard]] juce::String name() const
    {
        static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return isValid() ? juce::String(names[tonic]) + (minor ? "m" : "") : juce::String();
    }

    bool operator==(const MusicalKey& other) const { return tonic == other.tonic && minor == other.minor; }

    // The key shift (semitones, -5..+6) that brings a track in `track` into key with the key leader:
    // the same mode goes to the leader's root or its fifth, whichever is fewer semitones away; a major /
    // minor pair goes to the leader's relative key (owner decision 2026-10-01). 0 when either is unknown.
    [[nodiscard]] static int matchShift(const MusicalKey& track, const MusicalKey& leader)
    {
        if (! track.isValid() || ! leader.isValid())
            return 0;
        const auto toward = [&track](int targetTonic) { return nearest(targetTonic - track.tonic); };
        if (track.minor != leader.minor)
            return toward(leader.minor ? leader.tonic + 3 : leader.tonic - 3);
        const int root = toward(leader.tonic);
        const int fifth = toward(leader.tonic + 7);
        return std::abs(fifth) < std::abs(root) ? fifth : root;
    }

    [[nodiscard]] static int wrap(int semitones) { return ((semitones % 12) + 12) % 12; }
    [[nodiscard]] static int nearest(int semitones) // -5..+6
    {
        const int w = wrap(semitones);
        return w > 6 ? w - 12 : w;
    }
};
} // namespace nevo::audio
