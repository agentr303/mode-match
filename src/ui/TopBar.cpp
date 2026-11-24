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

    menuButton.setLookAndFeel(&look);
    menuButton.setTooltip("Presets, spiral layout, transposition, MIDI out, GPU drawing and About");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible(menuButton);

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
    menuButton.setBounds(r.removeFromRight(narrow ? 76 : 90).reduced(3, 2));
    clearButton.setBounds(r.removeFromRight(narrow ? 84 : 100).reduced(3, 2));
    a4Slider.setBounds(r.removeFromRight(narrow ? 96 : 130).reduced(3, 4));
    r.removeFromRight(8);
    const int w = juce::jmin(narrow ? 72 : 92, r.getWidth() / 4);
    for (auto& b : modeButtons)
        b.setBounds(r.removeFromLeft(w).reduced(3, 2));
}

void TopBar::showMenu()
{
    using namespace rk;
    juce::PopupMenu presetMenu, topMenu, rangeMenu, transposeMenu, midiMenu;
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetMenu.addItem(processor.getProgramName(i), true, processor.getCurrentProgram() == i, [this, i] { processor.setCurrentProgram(i); syncFromProcessor(); });
    for (int pc = 0; pc < 12; ++pc)
        topMenu.addItem(noteName(pc), true, processor.getTopNote() == pc, [this, pc] { processor.setTopNote(pc); });
    for (int i = 0; i < static_cast<int>(spiralRanges.size()); ++i)
        rangeMenu.addItem(spiralRanges[static_cast<std::size_t>(i)].name, true, processor.getRangeIndex() == i, [this, i] { processor.setRangeIndex(i); });
    for (int i = 0; i < static_cast<int>(transpositions.size()); ++i)
        transposeMenu.addItem(transpositions[static_cast<std::size_t>(i)].name, true, processor.getTransposition() == i,
                              [this, i] { processor.setTransposition(i); });
    transposeMenu.addSeparator();
    transposeMenu.addItem("Clear the target note", processor.getTuneTarget() >= 0, false, [this] { processor.setTuneTarget(-1); });
    const auto midi = processor.getMidiOut();
    midiMenu.addItem("Off", true, midi == MidiOut::off, [this] { processor.setMidiOut(MidiOut::off); });
    midiMenu.addItem("Key switch (tonic note C1-B1, CC 20 = scale type)", true, midi == MidiOut::keySwitch,
                     [this] { processor.setMidiOut(MidiOut::keySwitch); });
    midiMenu.addItem("Scale notes (held, C4-B4)", true, midi == MidiOut::scaleNotes, [this] { processor.setMidiOut(MidiOut::scaleNotes); });

    juce::PopupMenu menu;
    menu.addSubMenu("Presets", presetMenu);
    menu.addSeparator();
    menu.addSubMenu("Spiral: note at the top", topMenu);
    menu.addSubMenu("Spiral: range", rangeMenu);
    menu.addSubMenu("Tune mode: transposing instrument", transposeMenu);
    menu.addSubMenu("MIDI out (channel 1)", midiMenu);
    menu.addItem("GPU drawing (OpenGL)", true, processor.getGpuEnabled(), [this]
    {
        processor.setGpuEnabled(! processor.getGpuEnabled());
        if (onGpuChanged != nullptr)
            onGpuChanged();
    });
    menu.addSeparator();
    menu.addItem("About Mode Match...", [this] { if (onAbout != nullptr) onAbout(); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton));
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
