#include "PluginEditor.h"

ModeMatchEditor::ModeMatchEditor(ModeMatchProcessor& p) : AudioProcessorEditor(&p), processor(p), topBar(p)
{
    addAndMakeVisible(topBar);
    addAndMakeVisible(spiral);
    addAndMakeVisible(summary);
    addAndMakeVisible(timeline);
    // setResizeLimits resizes (and so calls resized(), which records the size): read the saved size first.
    const int width = processor.editorWidth, height = processor.editorHeight;
    setResizable(true, true);
    setResizeLimits(640, 420, 2400, 1600);
    setSize(width, height);
    startTimerHz(30);
}

ModeMatchEditor::~ModeMatchEditor()
{
    stopTimer();
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
    topBar.setBounds(r.removeFromTop(48));
    r.removeFromTop(8);
    const bool compact = getWidth() < 900;
    const int timelineHeight = compact ? 74 : juce::jlimit(90, 150, getHeight() / 6);
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
    spiral.update(snapshot, mode, processor.getReferenceA4());
    topBar.update(snapshot);
    if (snapshot.serial != lastSerial)
    {
        lastSerial = snapshot.serial;
        summary.update(snapshot, mode);
        timeline.update(snapshot);
    }
}
