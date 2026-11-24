#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// Title, the four modes (Listen / Analyze / Lock / Tune), A4 reference, Clear, input meter.
class TopBar final : public juce::Component
{
public:
    explicit TopBar(ModeMatchProcessor&);
    void update(const Snapshot&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Look final : juce::LookAndFeel_V4
    {
        Look();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    };

    void syncFromProcessor();

    ModeMatchProcessor& processor;
    Look look;
    std::array<juce::TextButton, 4> modeButtons;
    juce::TextButton clearButton { "Clear song" };
    juce::Slider a4Slider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    float level = 0.0f;
    juce::Rectangle<int> titleArea, meterArea;
};
