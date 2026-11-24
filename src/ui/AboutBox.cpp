#include "AboutBox.h"

#include "BinaryData.h"

#ifndef MM_VERSION_STRING
 #define MM_VERSION_STRING "dev"
#endif

AboutBox::AboutBox()
{
    using namespace rk;
    juce::String text;
    text << "Mode Match " << MM_VERSION_STRING << "\n\n"
         << "Copyright (C) 2026 agentr303.\n"
         << "Mode Match is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General "
            "Public License, version 3 (AGPLv3). It comes with ABSOLUTELY NO WARRANTY. The source for this build is the matching "
            "release tag on GitHub.\n\n"
         << "\"Camelot\" is a trademark of Mixed In Key LLC. Mode Match is not affiliated with Mixed In Key; the codes are used "
            "descriptively.\n\n"
         << "VST is a trademark of Steinberg Media Technologies GmbH.\n\n"
         << "----------------------------------------------------------------\n\n"
         << juce::String::fromUTF8(BinaryData::THIRD_PARTY_NOTICES_md, BinaryData::THIRD_PARTY_NOTICES_mdSize);
    notices.setMultiLine(true);
    notices.setReadOnly(true);
    notices.setScrollbarsShown(true);
    notices.setCaretVisible(false);
    notices.setFont(theme::font(13.0f));
    notices.setColour(juce::TextEditor::backgroundColourId, theme::well);
    notices.setColour(juce::TextEditor::textColourId, theme::ink);
    notices.setColour(juce::TextEditor::outlineColourId, theme::line);
    notices.setText(text, false);
    addAndMakeVisible(notices);

    sourceLink.setColour(juce::HyperlinkButton::textColourId, theme::live);
    addAndMakeVisible(sourceLink);
    closeButton.setColour(juce::TextButton::buttonColourId, theme::well);
    closeButton.setColour(juce::TextButton::textColourOffId, theme::ink);
    closeButton.onClick = [this] { setVisible(false); };
    addAndMakeVisible(closeButton);
}

juce::Rectangle<int> AboutBox::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(juce::jmin(640, getWidth() - 40), juce::jmin(520, getHeight() - 40));
}

void AboutBox::paint(juce::Graphics& g)
{
    g.fillAll(rk::theme::desk.withAlpha(0.75f));
    const auto panel = panelBounds().toFloat();
    rk::theme::drawPanel(g, panel);
    g.setColour(rk::theme::amber);
    g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre({ panel.getX() + 26.0f, panel.getY() + 30.0f }));
    g.setColour(rk::theme::ink);
    g.setFont(rk::theme::font(22.0f, true));
    g.drawText("Mode Match", panel.reduced(44.0f, 16.0f).removeFromTop(28.0f), juce::Justification::centredLeft);
    g.setColour(rk::theme::mute);
    g.setFont(rk::theme::font(13.0f));
    g.drawText(juce::String("Version ") + MM_VERSION_STRING + "  |  GNU AGPLv3", panel.reduced(44.0f, 16.0f).withTrimmedTop(26.0f).removeFromTop(18.0f),
               juce::Justification::centredLeft);
}

void AboutBox::resized()
{
    auto r = panelBounds().reduced(16);
    r.removeFromTop(56);
    auto bottom = r.removeFromBottom(30);
    closeButton.setBounds(bottom.removeFromRight(90));
    sourceLink.setBounds(bottom.removeFromLeft(280));
    r.removeFromBottom(8);
    notices.setBounds(r);
}

void AboutBox::mouseDown(const juce::MouseEvent& e)
{
    if (! panelBounds().contains(e.getPosition()))
        setVisible(false);
}
