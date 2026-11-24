#pragma once

#include <array>

// Display and workflow settings shared by the processor (state, presets, MIDI out) and the editor.
namespace rk
{
// Spiral range (PLAN.md 2: "grid range (e.g. E1-C8)"), in MIDI notes.
struct SpiralRange
{
    const char* name;
    float lowMidi, highMidi;
};
inline constexpr std::array<SpiralRange, 6> spiralRanges {{
    { "C1 - C8 (full)", 24.0f, 108.0f },
    { "E1 - C8", 28.0f, 108.0f },
    { "A0 - C8 (piano)", 21.0f, 108.0f },
    { "C2 - C7", 36.0f, 96.0f },
    { "E1 - C5 (bass)", 28.0f, 72.0f },
    { "C3 - C8 (treble)", 48.0f, 108.0f },
}};

// Transposing instruments: written pitch = concert pitch + semitones (the tuner and Tune mode show written names).
struct Transposition
{
    const char* name;
    int semitones;
};
inline constexpr std::array<Transposition, 4> transpositions {{
    { "Concert pitch (C)", 0 },
    { "Bb instrument (trumpet, clarinet, tenor sax)", 2 },
    { "Eb instrument (alto / baritone sax)", 9 },
    { "F instrument (horn, cor anglais)", 7 },
}};

// MIDI output (PLAN.md milestone 6): off, a key switch, or the scale held as notes.
enum class MidiOut { off = 0, keySwitch, scaleNotes };

// Factory presets (the host's program list).
struct Preset
{
    const char* name;
    float a4;
    int topNote, range, transposition;
};
inline constexpr std::array<Preset, 7> presets {{
    { "Default", 440.0f, 0, 0, 0 },
    { "Baroque (A4 = 415 Hz)", 415.0f, 0, 0, 0 },
    { "Orchestra (A4 = 442 Hz)", 442.0f, 0, 0, 0 },
    { "Bb instrument", 440.0f, 0, 3, 1 },
    { "Eb instrument", 440.0f, 0, 3, 2 },
    { "F instrument", 440.0f, 0, 3, 3 },
    { "Bass (E1 - C5)", 440.0f, 4, 4, 0 },
}};

// Scale types in MIDI key-switch order (CC 20 value), matching TuningScale::type.
inline constexpr std::array<const char*, 9> scaleTypes {{
    "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
}};
} // namespace rk
