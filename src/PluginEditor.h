#pragma once

#include "PluginProcessor.h"
#include "ui/SpiralView.h"
#include "ui/SummaryPanel.h"
#include "ui/TimelineView.h"
#include "ui/TopBar.h"

class ModeMatchEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ModeMatchEditor(ModeMatchProcessor&);
    ~ModeMatchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh() { timerCallback(); } // pull the latest snapshot now (the render tool)

private:
    void timerCallback() override;

    ModeMatchProcessor& processor;
    TopBar topBar;
    SpiralView spiral;
    SummaryPanel summary;
    TimelineView timeline;
    juce::TooltipWindow tooltips { this, 600 };
    Snapshot snapshot;
    juce::uint32 lastSerial = 0;
};
