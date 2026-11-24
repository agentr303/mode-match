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
    // trackName / trackKey: the host's track name and the Camelot label found in it (-1: none).
    void update(const Snapshot&, ViewMode, const juce::String& trackName, int trackKey);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Snapshot data;
    ViewMode viewMode = ViewMode::analyze;
    CamelotWheel wheel;
    juce::TextButton copyButton { "Copy" };
    juce::String trackName;
    int trackKey = -1;
    juce::Rectangle<int> textArea, barsArea;
    static constexpr int scaleRowY = 144; // header + key + mode line + confidence
};
