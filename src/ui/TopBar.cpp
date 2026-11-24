#include "TopBar.h"

using namespace rk;

TopBar::Look::Look()
{
    setColour(juce::TextButton::buttonColourId, theme::well);
    setColour(juce::TextButton::buttonOnColourId, theme::amber);
    setColour(juce::TextButton::textColourOffId, theme::ink);
    setColour(juce::TextButton::textColourOnId, theme::desk);
    setColour(juce::Slider::backgroundColourId, theme::well);
    setColour(juce::Slider::trackColourId, theme::line);
    setColour(juce::Slider::textBoxTextColourId, theme::ink);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void TopBar::Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    auto c = b.getToggleState() ? findColour(juce::TextButton::buttonOnColourId) : base;
    if (over)
        c = c.brighter(0.12f);
    if (down)
        c = c.darker(0.15f);
    g.setColour(c);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(theme::line);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
}

TopBar::TopBar(ModeMatchProcessor& p) : processor(p)
{
    static const char* names[] = { "Listen", "Analyze", "Lock", "Tune" };
    static const char* tips[] = { "Show what is playing without adding it to the song key",
                                  "Add what plays to this instance's song key (default)",
                                  "Freeze the song key summary",
                                  "Tuner: the spiral shows how far each partial is from the nearest note" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = modeButtons[static_cast<std::size_t>(i)];
        b.setButtonText(names[i]);
        b.setTooltip(tips[i]);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(1001);
        b.setLookAndFeel(&look);
        b.onClick = [this, i]
        {
            if (modeButtons[static_cast<std::size_t>(i)].getToggleState())
                processor.setViewMode(static_cast<ViewMode>(i));
        };
        addAndMakeVisible(b);
    }
    clearButton.setLookAndFeel(&look);
    clearButton.setTooltip("Forget everything this instance has heard and start the song key again");
    clearButton.onClick = [this] { processor.requestClear(); };
    addAndMakeVisible(clearButton);

    a4Slider.setLookAndFeel(&look);
    a4Slider.setRange(415.0, 466.0, 0.1);
    a4Slider.setTextValueSuffix(" Hz");
    a4Slider.setTooltip("Reference A4 for the spiral and the tuner");
    a4Slider.setDoubleClickReturnValue(true, 440.0);
    a4Slider.onValueChange = [this] { processor.setReferenceA4(static_cast<float>(a4Slider.getValue())); };
    addAndMakeVisible(a4Slider);
    syncFromProcessor();
}

void TopBar::syncFromProcessor()
{
    modeButtons[static_cast<std::size_t>(processor.getViewMode())].setToggleState(true, juce::dontSendNotification);
    a4Slider.setValue(processor.getReferenceA4(), juce::dontSendNotification);
}

void TopBar::update(const Snapshot& s)
{
    level = std::max(level * 0.88f, s.inputLevel);
    syncFromProcessor(); // the host may have restored a state
    repaint(meterArea);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced(4, 6);
    const bool narrow = getWidth() < 900;
    titleArea = r.removeFromLeft(narrow ? 110 : 190);
    meterArea = r.removeFromRight(narrow ? 40 : 90).reduced(6, 10);
    clearButton.setBounds(r.removeFromRight(narrow ? 84 : 100).reduced(3, 2));
    a4Slider.setBounds(r.removeFromRight(narrow ? 96 : 130).reduced(3, 4));
    r.removeFromRight(8);
    const int w = juce::jmin(narrow ? 72 : 92, r.getWidth() / 4);
    for (auto& b : modeButtons)
        b.setBounds(r.removeFromLeft(w).reduced(3, 2));
}

void TopBar::paint(juce::Graphics& g)
{
    auto t = titleArea.toFloat();
    g.setColour(theme::amber);
    g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ t.getX() + 10.0f, t.getCentreY() }));
    g.setColour(theme::ink);
    g.setFont(theme::font(getWidth() < 900 ? 14.0f : 17.0f, true));
    g.drawText(JucePlugin_Name, t.withTrimmedLeft(24.0f), juce::Justification::centredLeft);

    // Input meter (dBFS, -60..0).
    auto m = meterArea.toFloat();
    g.setColour(theme::well);
    g.fillRoundedRectangle(m, 3.0f);
    const float db = juce::Decibels::gainToDecibels(level, -60.0f);
    const float fill = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour(db > -3.0f ? theme::red : theme::green);
    g.fillRoundedRectangle(m.withWidth(m.getWidth() * fill), 3.0f);
}
