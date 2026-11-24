#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/KeyTypes.h"

// The mockup's palette (Design canvas: desk / panel / well / line / ink / mute + amber, red, live, green).
namespace rk::theme
{
inline const juce::Colour desk { 0xff0d1117 };
inline const juce::Colour panel { 0xff161c26 };
inline const juce::Colour well { 0xff0f141c };
inline const juce::Colour line { 0xff263142 };
inline const juce::Colour ink { 0xffe6ebf2 };
inline const juce::Colour mute { 0xff8b97a8 };
inline const juce::Colour amber { 0xfff2b45a };
inline const juce::Colour red { 0xffff5d73 };
inline const juce::Colour live { 0xff7fd3ff };
inline const juce::Colour green { 0xff6be3a4 };

inline juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

// A key's colour: the hue walks the Camelot wheel (neighbours look alike); the B ring (major) is lighter.
inline juce::Colour keyColour(int keyIndex, float alpha = 1.0f)
{
    if (keyIndex < 0)
        return mute.withAlpha(alpha);
    const int number = toMusicalKey(keyIndex).camelotNumber(); // 1..12
    const float hue = std::fmod(static_cast<float>(number - 1) / 12.0f + 0.55f, 1.0f);
    return juce::Colour::fromHSV(hue, keyIsMinor(keyIndex) ? 0.62f : 0.48f, keyIsMinor(keyIndex) ? 0.86f : 0.97f, alpha);
}

inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(panel);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(line);
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
}

inline juce::String chordName(int chord) { return rk::chordName(chord); }
} // namespace rk::theme
