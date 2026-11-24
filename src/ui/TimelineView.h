#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// The key map over the song: one cell per beat (or half second), coloured by the key heard there, with the key
// changes labelled and the playhead.
class TimelineView final : public juce::Component
{
public:
    void update(const Snapshot&);
    void paint(juce::Graphics&) override;

private:
    Snapshot data;
};
