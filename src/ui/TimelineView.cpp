#include "TimelineView.h"

#include <climits>

using namespace rk;

void TimelineView::update(const Snapshot& s)
{
    data = s;
    repaint();
}

void TimelineView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    theme::drawPanel(g, bounds);
    auto r = bounds.reduced(14.0f, 8.0f);
    auto header = r.removeFromTop(16.0f);
    g.setFont(theme::font(12.0f, true));
    g.setColour(theme::mute);
    g.drawText(data.ara ? "KEY MAP + CHORDS  (ARA: the clips' audio, one cell per 0.5 s)"
                        : data.hostTimeline ? "KEY MAP + CHORDS  (host timeline, one cell per beat)"
                                            : "KEY MAP + CHORDS  (no host timeline: one cell per 0.5 s)",
               header, juce::Justification::left);

    if (data.timeline.empty())
    {
        g.setColour(theme::mute.withAlpha(0.6f));
        g.setFont(theme::font(13.0f));
        g.drawText("Nothing analysed yet", r, juce::Justification::centred);
        return;
    }

    const int first = juce::jmin(data.timeline.front().index, static_cast<int>(std::floor(data.beat)));
    const int last = juce::jmax(data.timeline.back().index, static_cast<int>(std::floor(data.beat))) + 1;
    const int span = juce::jmax(16, last - first);
    auto lanes = r.removeFromBottom(r.getHeight() - 16.0f);
    auto labels = r;
    // Key lane on top, chord lane below (PLAN.md 2: "key lane + chord lane, with playhead").
    auto chordLane = lanes.removeFromBottom(juce::jmax(14.0f, lanes.getHeight() * 0.38f));
    lanes.removeFromBottom(3.0f);
    auto lane = lanes;
    const float cellW = lane.getWidth() / static_cast<float>(span);
    const auto xFor = [&](double index) { return lane.getX() + static_cast<float>(index - first) * cellW; };

    g.setColour(theme::well);
    g.fillRoundedRectangle(lane, 4.0f);
    g.fillRoundedRectangle(chordLane, 4.0f);

    // Chords: one segment per run of the same chord, named when there is room.
    {
        int runChord = -2;
        double runStart = 0.0;
        const auto drawRun = [&](double endIndex)
        {
            if (runChord < 0)
                return;
            const float x0 = xFor(runStart), x1 = xFor(endIndex);
            const int asKey = keyIndex(chordRoot(runChord), chordIsMinorType(runChord));
            auto seg = juce::Rectangle<float>(x0, chordLane.getY() + 1.0f, juce::jmax(1.0f, x1 - x0 - 1.0f), chordLane.getHeight() - 2.0f);
            g.setColour(theme::keyColour(asKey, 0.55f));
            g.fillRoundedRectangle(seg, 2.0f);
            if (seg.getWidth() > 26.0f && seg.getHeight() >= 12.0f)
            {
                g.setColour(theme::desk);
                g.setFont(theme::font(juce::jmin(12.0f, seg.getHeight() - 2.0f), true));
                g.drawText(theme::chordName(runChord), seg.reduced(3.0f, 0.0f), juce::Justification::centredLeft, true);
            }
        };
        int prevIndex = INT_MIN;
        for (const auto& c : data.timeline)
        {
            if (c.chord != runChord || c.index != prevIndex + 1)
            {
                drawRun(prevIndex + 1);
                runChord = c.chord;
                runStart = c.index;
            }
            prevIndex = c.index;
        }
        drawRun(prevIndex + 1);
    }

    float maxWeight = 0.0f;
    for (const auto& c : data.timeline)
        maxWeight = std::max(maxWeight, c.weight);

    // Cells; the height shows how much tonal material was there.
    int segmentKey = -2;
    float segmentStart = 0.0f;
    const auto labelSegment = [&](float x0, float x1, int key)
    {
        if (key < 0 || x1 - x0 < 34.0f)
            return;
        g.setColour(theme::keyColour(key));
        g.setFont(theme::font(11.0f, true));
        g.drawText(keyShortName(key) + " " + camelotCode(key), juce::Rectangle<float>(x0 + 3.0f, labels.getY(), x1 - x0 - 3.0f, labels.getHeight()),
                   juce::Justification::centredLeft, true);
    };
    for (const auto& c : data.timeline)
    {
        const float x = xFor(c.index);
        const float h = maxWeight > 0.0f ? juce::jmax(0.15f, c.weight / maxWeight) * lane.getHeight() : 0.0f;
        g.setColour(theme::keyColour(c.localKey, 0.85f));
        g.fillRect(juce::Rectangle<float>(x, lane.getBottom() - h, juce::jmax(1.0f, cellW - (cellW > 4.0f ? 1.0f : 0.0f)), h));
        if (c.localKey != segmentKey)
        {
            if (segmentKey != -2)
                labelSegment(segmentStart, x, segmentKey);
            if (segmentKey >= 0 && c.localKey >= 0)
            {
                g.setColour(theme::ink.withAlpha(0.8f));
                g.drawVerticalLine(static_cast<int>(x), lane.getY(), lane.getBottom());
            }
            segmentKey = c.localKey;
            segmentStart = x;
        }
    }
    labelSegment(segmentStart, xFor(data.timeline.back().index + 1), segmentKey);

    // Bar lines.
    if (data.hostTimeline && data.beatsPerBar > 0)
    {
        const int barEvery = cellW * data.beatsPerBar < 6.0f ? data.beatsPerBar * 8 : data.beatsPerBar;
        g.setColour(theme::line.withAlpha(0.7f));
        for (int b = (first / barEvery) * barEvery; b <= last; b += barEvery)
            if (b >= first)
                g.drawVerticalLine(static_cast<int>(xFor(b)), lane.getY(), lane.getY() + 4.0f);
    }

    // Playhead.
    if (data.playing)
    {
        const float x = xFor(data.beat);
        g.setColour(theme::amber);
        g.fillRect(juce::Rectangle<float>(x - 1.0f, lane.getY() - 2.0f, 2.0f, chordLane.getBottom() - lane.getY() + 4.0f));
    }
}
