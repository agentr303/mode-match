#include "PluginEditor.h"

ModeMatchEditor::ModeMatchEditor(ModeMatchProcessor& p) : AudioProcessorEditor(&p), processor(p), topBar(p)
{
    addAndMakeVisible(topBar);
    addAndMakeVisible(spiral);
    addAndMakeVisible(summary);
    addAndMakeVisible(timeline);
    addChildComponent(about);
    topBar.onAbout = [this] { about.setVisible(true); about.toFront(true); };
    topBar.onGpuChanged = [this] { applyGpuSetting(); };
    spiral.onTargetClicked = [this](int pc) { processor.setTuneTarget(pc); };
    // setResizeLimits resizes (and so calls resized(), which records the size): read the saved size first.
    const int width = processor.editorWidth, height = processor.editorHeight;
    setResizable(true, true);
    setResizeLimits(640, 420, 2400, 1600);
    setSize(width, height);
    applyGpuSetting();
    processor.editorOpened();
    startTimerHz(30);
}

ModeMatchEditor::~ModeMatchEditor()
{
    stopTimer();
    processor.editorClosed();
    openGL.detach();
}

void ModeMatchEditor::applyGpuSetting()
{
    if (processor.getGpuEnabled() && ! openGL.isAttached())
        openGL.attachTo(*this);
    else if (! processor.getGpuEnabled() && openGL.isAttached())
        openGL.detach();
}

void ModeMatchEditor::paint(juce::Graphics& g)
{
    g.fillAll(rk::theme::desk);
}

void ModeMatchEditor::resized()
{
    processor.editorWidth = getWidth();
    processor.editorHeight = getHeight();

    auto r = getLocalBounds().reduced(10);
    about.setBounds(getLocalBounds());
    topBar.setBounds(r.removeFromTop(48));
    r.removeFromTop(8);
    const bool compact = getWidth() < 900;
    const int timelineHeight = compact ? 84 : juce::jlimit(100, 170, getHeight() / 5);
    timeline.setBounds(r.removeFromBottom(timelineHeight));
    r.removeFromBottom(8);
    const int summaryWidth = compact ? juce::jmax(250, r.getWidth() * 44 / 100) : juce::jlimit(340, 520, r.getWidth() * 36 / 100);
    summary.setBounds(r.removeFromRight(summaryWidth));
    r.removeFromRight(8);
    spiral.setBounds(r);
}

void ModeMatchEditor::timerCallback()
{
    processor.copySnapshot(snapshot);
    const auto mode = processor.getViewMode();
    SpiralSettings settings;
    settings.topNote = processor.getTopNote();
    const auto& range = rk::spiralRanges[static_cast<std::size_t>(processor.getRangeIndex())];
    settings.lowMidi = range.lowMidi;
    settings.highMidi = range.highMidi;
    const auto& transposition = rk::transpositions[static_cast<std::size_t>(processor.getTransposition())];
    settings.transpose = transposition.semitones;
    settings.transpositionName = juce::String(transposition.name).upToFirstOccurrenceOf(" (", false, false);
    settings.target = processor.getTuneTarget();
    spiral.update(snapshot, mode, processor.getReferenceA4(), settings);
    topBar.update(snapshot);
    if (snapshot.serial != lastSerial)
    {
        lastSerial = snapshot.serial;
        summary.update(snapshot, mode, processor.getTrackName(), processor.getTrackKeyHint());
        timeline.update(snapshot);
    }
}
