#pragma once

#include <juce_opengl/juce_opengl.h>

#include "PluginProcessor.h"
#include "ui/AboutBox.h"
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
    void applyGpuSetting();

    ModeMatchProcessor& processor;
    TopBar topBar;
    SpiralView spiral;
    SummaryPanel summary;
    TimelineView timeline;
    AboutBox about;
    juce::TooltipWindow tooltips { this, 600 };
    // GPU drawing (PLAN.md 5): JUCE's OpenGL renderer for the whole editor; off falls back to the CPU renderer.
    juce::OpenGLContext openGL;
    Snapshot snapshot;
    juce::uint32 lastSerial = 0;
};
