#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// The song key: the summative side (PLAN.md 2). Big key name with its Camelot code, the mode line, confidence,
// tuning, pitch-class presence, and a Camelot wheel with the compatible keys marked.
class CamelotWheel final : public juce::Component
{
public:
    void setKey(int keyIndex, int localKey);
    void paint(juce::Graphics&) override;

private:
    int key = -1, local = -1;
};

class SummaryPanel final : public juce::Component
{
public:
    SummaryPanel();
    void update(const Snapshot&, ViewMode);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Snapshot data;
    ViewMode viewMode = ViewMode::analyze;
    CamelotWheel wheel;
    juce::Rectangle<int> textArea, barsArea;
};
