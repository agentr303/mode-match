#pragma once

#include "Theme.h"

// About Mode Match: version, licence, source link, the Camelot trademark credit and the third-party notices
// (THIRD_PARTY_NOTICES.md, embedded). Shown over the editor; click outside the panel or Close to dismiss.
class AboutBox final : public juce::Component
{
public:
    AboutBox();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> panelBounds() const;

    juce::TextEditor notices;
    juce::HyperlinkButton sourceLink { "github.com/agentr303/mode-match", juce::URL("https://github.com/agentr303/mode-match") };
    juce::TextButton closeButton { "Close" };
};
