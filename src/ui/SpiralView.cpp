#include "SpiralView.h"

using namespace rk;

namespace
{
constexpr float lowMidi = 24.0f, highMidi = 108.0f; // C1 .. C8
constexpr float twoPi = juce::MathConstants<float>::twoPi;
} // namespace

void SpiralView::update(const Snapshot& s, ViewMode mode, float a4)
{
    data = s;
    viewMode = mode;
    referenceA4 = a4;

    // Pitch-class petals: the partials' levels folded to 12 classes, with a peak hold that falls ~15 % per frame.
    std::array<float, 12> now {};
    for (const auto& p : data.partials)
    {
        int pc = static_cast<int>(std::lround(p.midi)) % 12;
        if (pc < 0)
            pc += 12;
        now[static_cast<std::size_t>(pc)] += p.level;
    }
    float maxNow = 0.0f;
    for (auto v : now)
        maxNow = std::max(maxNow, v);
    for (int i = 0; i < 12; ++i)
    {
        const float v = maxNow > 0.0f ? now[static_cast<std::size_t>(i)] / maxNow * juce::jmin(1.0f, maxNow) : 0.0f;
        pcEnergy[static_cast<std::size_t>(i)] = std::max(pcEnergy[static_cast<std::size_t>(i)] * 0.85f, v);
    }

    // Tuner: smooth the YIN reading, snap to the nearest note.
    if (data.pitch.valid && data.pitch.clarity > 0.6f)
    {
        if (! tunerValid || std::abs(data.pitch.midi - tunerMidi) > 0.6f)
            tunerMidi = data.pitch.midi;
        else
            tunerMidi += (data.pitch.midi - tunerMidi) * 0.35f;
        tunerValid = true;
        tunerCents = (tunerMidi - std::round(tunerMidi)) * 100.0f;
    }
    else if (data.inputLevel < 1.0e-3f)
        tunerValid = false;
    repaint();
}

float SpiralView::radiusFor(float midi, float radius)
{
    return radius * (0.14f + 0.78f * juce::jlimit(0.0f, 1.0f, (midi - lowMidi) / (highMidi - lowMidi)));
}

juce::Point<float> SpiralView::pointFor(float midi, juce::Point<float> centre, float radius) const
{
    const float angle = twoPi * std::fmod(midi - lowMidi + 1200.0f, 12.0f) / 12.0f; // C at the top, clockwise
    const float r = radiusFor(midi, radius);
    return { centre.x + r * std::sin(angle), centre.y - r * std::cos(angle) };
}

void SpiralView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    theme::drawPanel(g, bounds);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 34.0f;
    if (radius < 40.0f)
        return;

    g.setColour(theme::well);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

    // The song's scale, shaded as wedges behind everything; the tonic stronger.
    const auto& summary = data.summary;
    std::array<bool, 12> inScale {};
    int tonic = -1;
    if (summary.valid)
    {
        for (int pc : summary.scale())
            inScale[static_cast<std::size_t>(pc)] = true;
        tonic = summary.tonic();
    }
    const auto keyCol = theme::keyColour(summary.valid ? summary.displayCamelotKey() : -1);
    for (int pc = 0; pc < 12; ++pc)
    {
        const float a0 = twoPi * (pc - 0.5f) / 12.0f, a1 = twoPi * (pc + 0.5f) / 12.0f;
        if (inScale[static_cast<std::size_t>(pc)])
        {
            juce::Path wedge;
            wedge.addPieSegment(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), a0, a1, 0.12f);
            g.setColour(keyCol.withAlpha(pc == tonic ? 0.16f : 0.07f));
            g.fillPath(wedge);
        }
        // Spoke.
        const float a = twoPi * pc / 12.0f;
        g.setColour(theme::line.withAlpha(0.8f));
        g.drawLine(centre.x + radius * 0.12f * std::sin(a), centre.y - radius * 0.12f * std::cos(a), centre.x + radius * std::sin(a),
                   centre.y - radius * std::cos(a), 1.0f);
    }

    // Pitch-class petals on the rim.
    for (int pc = 0; pc < 12; ++pc)
    {
        const float e = pcEnergy[static_cast<std::size_t>(pc)];
        if (e < 0.02f)
            continue;
        const float a0 = twoPi * (pc - 0.42f) / 12.0f, a1 = twoPi * (pc + 0.42f) / 12.0f;
        juce::Path arc;
        const float r = radius + 4.0f + 7.0f * e;
        arc.addCentredArc(centre.x, centre.y, r, r, 0.0f, a0, a1, true);
        g.setColour((inScale[static_cast<std::size_t>(pc)] || ! summary.valid ? theme::live : theme::red).withAlpha(0.25f + 0.65f * e));
        g.strokePath(arc, juce::PathStrokeType(2.0f + 10.0f * e, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // The spiral itself.
    juce::Path spiralPath;
    for (float m = lowMidi; m <= highMidi; m += 0.05f)
    {
        const auto p = pointFor(m, centre, radius);
        if (m == lowMidi)
            spiralPath.startNewSubPath(p);
        else
            spiralPath.lineTo(p);
    }
    g.setColour(theme::line.brighter(0.35f));
    g.strokePath(spiralPath, juce::PathStrokeType(1.2f));

    // Note names round the outside; the characteristic note of a mode gets a dashed ring.
    for (int pc = 0; pc < 12; ++pc)
    {
        const float a = twoPi * pc / 12.0f;
        const float r = radius + 22.0f;
        const juce::Point<float> p { centre.x + r * std::sin(a), centre.y - r * std::cos(a) };
        const bool isTonic = pc == tonic;
        g.setColour(isTonic ? theme::amber : (inScale[static_cast<std::size_t>(pc)] ? theme::ink : theme::mute.withAlpha(0.6f)));
        g.setFont(theme::font(isTonic ? 15.0f : 13.0f, isTonic));
        g.drawText(noteName(pc), juce::Rectangle<float>(36.0f, 18.0f).withCentre(p), juce::Justification::centred);
        if (summary.valid && summary.mode.modal && pc == summary.mode.characteristicPc)
        {
            juce::Path ring;
            ring.addEllipse(juce::Rectangle<float>(28.0f, 28.0f).withCentre(p));
            juce::Path dashed;
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType(1.4f).createDashedStroke(dashed, ring, dashes, 2);
            g.setColour(theme::amber);
            g.fillPath(dashed);
        }
    }

    // Partials: quiet first so loud ones draw on top.
    for (auto it = data.partials.rbegin(); it != data.partials.rend(); ++it)
    {
        const auto& p = *it;
        if (p.midi < lowMidi - 0.5f || p.midi > highMidi + 0.5f)
            continue;
        const auto pos = pointFor(p.midi, centre, radius);
        const float size = 3.0f + 11.0f * p.level;
        juce::Colour c = theme::live.interpolatedWith(theme::green, p.stability);
        if (viewMode == ViewMode::tune)
        {
            const float cents = std::abs(p.midi - std::round(p.midi)) * 100.0f;
            c = cents < 5.0f ? theme::green : (cents < 15.0f ? theme::amber : theme::red);
        }
        g.setColour(c.withAlpha(0.18f * p.level));
        g.fillEllipse(juce::Rectangle<float>(size * 2.4f, size * 2.4f).withCentre(pos));
        g.setColour(c.withAlpha(0.35f + 0.65f * p.level));
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(pos));
    }

    if (viewMode == ViewMode::tune)
        paintTuner(g, centre, radius);
    else
    {
        // Centre: what is heard now.
        auto c = juce::Rectangle<float>(radius * 0.5f, radius * 0.24f).withCentre(centre);
        g.setColour(theme::desk.withAlpha(0.85f));
        g.fillRoundedRectangle(c, 8.0f);
        g.setColour(theme::keyColour(data.localKey));
        g.setFont(theme::font(juce::jlimit(12.0f, 22.0f, radius * 0.075f), true));
        g.drawText(keyShortName(data.localKey), c.removeFromTop(c.getHeight() * 0.58f), juce::Justification::centredBottom);
        g.setColour(theme::mute);
        g.setFont(theme::font(juce::jlimit(10.0f, 14.0f, radius * 0.05f)));
        g.drawText("now  " + theme::chordName(data.chord), c, juce::Justification::centredTop);
    }

    // Corner labels.
    auto top = bounds.reduced(14.0f, 10.0f);
    g.setFont(theme::font(12.0f, true));
    g.setColour(theme::mute);
    g.drawText("SPIRAL  A4 = " + juce::String(referenceA4, 1) + " Hz", top.removeFromTop(16.0f), juce::Justification::topLeft);
    if (viewMode == ViewMode::lock)
    {
        g.setColour(theme::amber);
        g.drawText("SUMMARY LOCKED", bounds.reduced(14.0f, 10.0f).removeFromTop(16.0f), juce::Justification::topRight);
    }
    else if (viewMode == ViewMode::listen)
    {
        g.setColour(theme::live);
        g.drawText("LISTEN (not adding to the song)", bounds.reduced(14.0f, 10.0f).removeFromTop(16.0f), juce::Justification::topRight);
    }
}

void SpiralView::paintTuner(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    auto box = juce::Rectangle<float>(radius * 0.78f, radius * 0.5f).withCentre(centre);
    g.setColour(theme::desk.withAlpha(0.9f));
    g.fillRoundedRectangle(box, 10.0f);
    if (! tunerValid)
    {
        g.setColour(theme::mute);
        g.setFont(theme::font(16.0f));
        g.drawText("play one note", box, juce::Justification::centred);
        return;
    }
    const int nearest = static_cast<int>(std::lround(tunerMidi));
    const float cents = tunerCents;
    const auto colour = std::abs(cents) < 3.0f ? theme::green : (std::abs(cents) < 12.0f ? theme::amber : theme::red);

    auto inner = box.reduced(10.0f);
    g.setColour(theme::ink);
    g.setFont(theme::font(juce::jlimit(18.0f, 54.0f, radius * 0.18f), true));
    g.drawText(noteName(((nearest % 12) + 12) % 12) + juce::String(nearest / 12 - 1), inner.removeFromTop(inner.getHeight() * 0.5f), juce::Justification::centred);
    g.setColour(colour);
    g.setFont(theme::mono(juce::jlimit(12.0f, 22.0f, radius * 0.07f), true));
    g.drawText((cents >= 0.0f ? "+" : "") + juce::String(cents, 1) + " c", inner.removeFromTop(inner.getHeight() * 0.45f), juce::Justification::centred);

    // A needle across -50..+50 cents.
    auto meter = inner.reduced(inner.getWidth() * 0.08f, 4.0f);
    g.setColour(theme::line);
    g.fillRoundedRectangle(meter.withHeight(3.0f).withCentre(meter.getCentre()), 1.5f);
    g.setColour(theme::mute);
    g.fillRect(juce::Rectangle<float>(1.5f, meter.getHeight()).withCentre(meter.getCentre()));
    const float x = meter.getX() + meter.getWidth() * (0.5f + juce::jlimit(-50.0f, 50.0f, cents) / 100.0f);
    g.setColour(colour);
    g.fillRoundedRectangle(juce::Rectangle<float>(5.0f, meter.getHeight()).withCentre({ x, meter.getCentreY() }), 2.0f);
}
