#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// The realtime spiral: one turn per octave (C1 in the middle, C8 outside), pitch class as the angle (C at the top,
// clockwise). Partials sit on the spiral where they sound, sized by level, coloured by stability. The song's scale is
// shaded behind them; in Tune mode the centre becomes a tuner.
class SpiralView final : public juce::Component
{
public:
    void update(const Snapshot& s, ViewMode mode, float a4);
    void paint(juce::Graphics&) override;

private:
    [[nodiscard]] juce::Point<float> pointFor(float midi, juce::Point<float> centre, float radius) const;
    [[nodiscard]] static float radiusFor(float midi, float radius);
    void paintTuner(juce::Graphics&, juce::Point<float> centre, float radius);

    Snapshot data;
    ViewMode viewMode = ViewMode::analyze;
    float referenceA4 = 440.0f;
    std::array<float, 12> pcEnergy {};
    float tunerMidi = 0.0f, tunerCents = 0.0f;
    bool tunerValid = false;
};
