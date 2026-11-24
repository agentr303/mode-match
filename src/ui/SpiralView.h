#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// Layout and Tune-mode settings the spiral reads from the processor each frame.
struct SpiralSettings
{
    int topNote = 0;                          // pitch class at 12 o'clock
    float lowMidi = 24.0f, highMidi = 108.0f; // the spiral's range (centre .. rim)
    int transpose = 0;                        // written = concert + transpose (Tune mode names)
    juce::String transpositionName;           // shown in Tune mode when transposing
    int target = -1;                          // Tune mode target pitch class, or -1
};

// The realtime spiral: one turn per octave, pitch class as the angle (the chosen note at the top, clockwise). Partials
// sit on the spiral where they sound: size = loudness, hue = pitch class in circle-of-fifths order (related notes look
// alike), brightness = stability, and a red halo when the note is outside the song's scale. The scale is shaded
// behind them; when the key heard now differs from the song key, its notes are ticked inside the rim. The centre shows
// the key and chord now with a +-50 cent drift ring for the strongest note; in Tune mode it becomes a tuner with a
// target note (click a note) and a zoomed 7-semitone ruler.
class SpiralView final : public juce::Component
{
public:
    void update(const Snapshot& s, ViewMode mode, float a4, const SpiralSettings& settings);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::function<void(int)> onTargetClicked; // Tune mode: the pitch class clicked (-1 clears)

private:
    [[nodiscard]] float angleFor(float midiOrPc) const;
    [[nodiscard]] juce::Point<float> pointFor(float midi, juce::Point<float> centre, float radius) const;
    [[nodiscard]] float radiusFor(float midi, float radius) const;
    [[nodiscard]] juce::String labelFor(int pc) const;
    void paintTuner(juce::Graphics&, juce::Point<float> centre, float radius);
    void paintDriftRing(juce::Graphics&, juce::Point<float> centre, float radius);
    void geometry(juce::Point<float>& centre, float& radius) const;

    Snapshot data;
    ViewMode viewMode = ViewMode::analyze;
    float referenceA4 = 440.0f;
    SpiralSettings settings;
    std::array<float, 12> pcEnergy {};
    float tunerMidi = 0.0f, tunerCents = 0.0f;
    bool tunerValid = false;
};
