#include "SpiralView.h"

using namespace rk;

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// Hue by pitch class in circle-of-fifths order: C, G, D ... sit next to each other on the colour wheel.
juce::Colour pitchColour(int pc, float stability, float alpha)
{
    const float hue = static_cast<float>((((pc % 12) + 12) % 12 * 7) % 12) / 12.0f;
    return juce::Colour::fromHSV(hue, 0.55f, 0.55f + 0.45f * juce::jlimit(0.0f, 1.0f, stability), alpha);
}

juce::Colour centsColour(float cents)
{
    const float a = std::abs(cents);
    return a < 3.0f ? theme::green : (a < 12.0f ? theme::amber : theme::red);
}
} // namespace

void SpiralView::update(const Snapshot& s, ViewMode mode, float a4, const SpiralSettings& newSettings)
{
    data = s;
    viewMode = mode;
    referenceA4 = a4;
    settings = newSettings;

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

    // Tuner and drift ring: smooth the YIN reading.
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

float SpiralView::angleFor(float midiOrPc) const
{
    return twoPi * std::fmod(midiOrPc - static_cast<float>(settings.topNote) + 1200.0f, 12.0f) / 12.0f; // clockwise from 12 o'clock
}

float SpiralView::radiusFor(float midi, float radius) const
{
    const float span = juce::jmax(12.0f, settings.highMidi - settings.lowMidi);
    return radius * (0.14f + 0.78f * juce::jlimit(0.0f, 1.0f, (midi - settings.lowMidi) / span));
}

juce::Point<float> SpiralView::pointFor(float midi, juce::Point<float> centre, float radius) const
{
    const float angle = angleFor(midi);
    const float r = radiusFor(midi, radius);
    return { centre.x + r * std::sin(angle), centre.y - r * std::cos(angle) };
}

juce::String SpiralView::labelFor(int pc) const
{
    // Tune mode names notes as the player reads them (written pitch).
    return noteName(viewMode == ViewMode::tune ? pc + settings.transpose : pc);
}

void SpiralView::geometry(juce::Point<float>& centre, float& radius) const
{
    const auto bounds = getLocalBounds().toFloat();
    centre = bounds.getCentre();
    radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 34.0f;
}

void SpiralView::mouseDown(const juce::MouseEvent& e)
{
    if (viewMode != ViewMode::tune || onTargetClicked == nullptr)
        return;
    juce::Point<float> centre;
    float radius = 0.0f;
    geometry(centre, radius);
    const auto d = e.position - centre;
    if (d.getDistanceFromOrigin() < radius * 0.3f || d.getDistanceFromOrigin() > radius + 40.0f)
        return; // the tuner box or outside the spiral
    float a = std::atan2(d.x, -d.y);
    if (a < 0.0f)
        a += twoPi;
    const int pc = (static_cast<int>(std::lround(a / twoPi * 12.0f)) + settings.topNote) % 12;
    onTargetClicked(pc == settings.target ? -1 : pc);
}

void SpiralView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    theme::drawPanel(g, bounds);
    juce::Point<float> centre;
    float radius = 0.0f;
    geometry(centre, radius);
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
        const float a = angleFor(static_cast<float>(pc));
        const float a0 = a - twoPi / 24.0f, a1 = a + twoPi / 24.0f;
        const auto disc = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
        if (inScale[static_cast<std::size_t>(pc)])
        {
            juce::Path wedge;
            wedge.addPieSegment(disc, a0, a1, 0.12f);
            g.setColour(keyCol.withAlpha(pc == tonic ? 0.16f : 0.07f));
            g.fillPath(wedge);
        }
        if (viewMode == ViewMode::tune && pc == settings.target)
        {
            juce::Path wedge;
            wedge.addPieSegment(disc, a0, a1, 0.12f);
            g.setColour(theme::amber.withAlpha(0.12f));
            g.fillPath(wedge);
            g.setColour(theme::amber.withAlpha(0.7f));
            g.strokePath(wedge, juce::PathStrokeType(1.5f));
        }
        g.setColour(theme::line.withAlpha(0.8f));
        g.drawLine(centre.x + radius * 0.12f * std::sin(a), centre.y - radius * 0.12f * std::cos(a), centre.x + radius * std::sin(a),
                   centre.y - radius * std::cos(a), 1.0f);
    }

    // The key heard now, when it is not the song key: its scale ticked inside the rim.
    const int songKey = summary.valid ? summary.majorMinorKey : -1;
    if (data.localKey >= 0 && summary.valid && data.localKey != songKey && data.localKey != summary.displayCamelotKey())
    {
        const auto iv = modeIntervals(keyIsMinor(data.localKey) ? Mode::aeolian : Mode::ionian);
        for (int step : iv)
        {
            const int pc = (keyTonic(data.localKey) + step) % 12;
            const float a = angleFor(static_cast<float>(pc));
            const float r0 = radius - (step == 0 ? 14.0f : 9.0f), r1 = radius - 1.0f;
            g.setColour(theme::keyColour(data.localKey).withAlpha(0.95f));
            g.drawLine(centre.x + r0 * std::sin(a), centre.y - r0 * std::cos(a), centre.x + r1 * std::sin(a), centre.y - r1 * std::cos(a),
                       step == 0 ? 4.0f : 2.5f);
        }
    }

    // Pitch-class petals on the rim.
    for (int pc = 0; pc < 12; ++pc)
    {
        const float e = pcEnergy[static_cast<std::size_t>(pc)];
        if (e < 0.02f)
            continue;
        const float a = angleFor(static_cast<float>(pc));
        juce::Path arc;
        const float r = radius + 4.0f + 7.0f * e;
        arc.addCentredArc(centre.x, centre.y, r, r, 0.0f, a - twoPi * 0.035f, a + twoPi * 0.035f, true);
        g.setColour((inScale[static_cast<std::size_t>(pc)] || ! summary.valid ? theme::live : theme::red).withAlpha(0.25f + 0.65f * e));
        g.strokePath(arc, juce::PathStrokeType(2.0f + 10.0f * e, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // The spiral itself.
    juce::Path spiralPath;
    for (float m = settings.lowMidi; m <= settings.highMidi; m += 0.05f)
    {
        const auto p = pointFor(m, centre, radius);
        if (m == settings.lowMidi)
            spiralPath.startNewSubPath(p);
        else
            spiralPath.lineTo(p);
    }
    g.setColour(theme::line.brighter(0.35f));
    g.strokePath(spiralPath, juce::PathStrokeType(1.2f));

    // Note names round the outside; the characteristic note of a mode gets a dashed ring.
    for (int pc = 0; pc < 12; ++pc)
    {
        const float a = angleFor(static_cast<float>(pc));
        const float r = radius + 22.0f;
        const juce::Point<float> p { centre.x + r * std::sin(a), centre.y - r * std::cos(a) };
        const bool isTonic = pc == tonic;
        g.setColour(isTonic ? theme::amber : (inScale[static_cast<std::size_t>(pc)] ? theme::ink : theme::mute.withAlpha(0.6f)));
        g.setFont(theme::font(isTonic ? 15.0f : 13.0f, isTonic));
        g.drawText(labelFor(pc), juce::Rectangle<float>(36.0f, 18.0f).withCentre(p), juce::Justification::centred);
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
        if (p.midi < settings.lowMidi - 0.5f || p.midi > settings.highMidi + 0.5f)
            continue;
        const auto pos = pointFor(p.midi, centre, radius);
        const float size = 3.0f + 11.0f * p.level;
        const int pc = ((static_cast<int>(std::lround(p.midi)) % 12) + 12) % 12;
        juce::Colour c = pitchColour(pc, p.stability, 1.0f);
        if (viewMode == ViewMode::tune)
            c = centsColour(std::abs(p.midi - std::round(p.midi)) * 100.0f);
        g.setColour(c.withAlpha(0.18f * p.level));
        g.fillEllipse(juce::Rectangle<float>(size * 2.4f, size * 2.4f).withCentre(pos));
        g.setColour(c.withAlpha(0.35f + 0.65f * p.level));
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(pos));
        if (summary.valid && ! inScale[static_cast<std::size_t>(pc)] && p.level > 0.15f)
        {
            g.setColour(theme::red.withAlpha(0.4f + 0.6f * p.level));
            g.drawEllipse(juce::Rectangle<float>(size + 6.0f, size + 6.0f).withCentre(pos), 1.6f);
        }
    }

    if (viewMode == ViewMode::tune)
        paintTuner(g, centre, radius);
    else
    {
        // Centre: what is heard now, inside the drift ring.
        paintDriftRing(g, centre, radius);
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
    else if (viewMode == ViewMode::tune)
    {
        g.setColour(theme::mute);
        const juce::String target = settings.target >= 0 ? "target " + labelFor(settings.target) : juce::String("click a note to set a target");
        g.drawText(target + (settings.transpose != 0 ? "  |  " + settings.transpositionName : juce::String()),
                   bounds.reduced(14.0f, 10.0f).removeFromTop(16.0f), juce::Justification::topRight);
    }
}

void SpiralView::paintDriftRing(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // +-50 cents over the top half of a ring round the centre readout, for the strongest fundamental (YIN).
    const float r = radius * 0.34f;
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour(theme::line);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(theme::mute.withAlpha(0.8f));
    g.drawLine(centre.x, centre.y - r - 5.0f, centre.x, centre.y - r + 5.0f, 1.2f);
    if (! tunerValid)
        return;
    const float a = juce::jlimit(-50.0f, 50.0f, tunerCents) / 50.0f * juce::MathConstants<float>::halfPi;
    const auto colour = centsColour(tunerCents);
    juce::Path fill;
    fill.addCentredArc(centre.x, centre.y, r, r, 0.0f, 0.0f, a, true);
    g.setColour(colour.withAlpha(0.8f));
    g.strokePath(fill, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ centre.x + r * std::sin(a), centre.y - r * std::cos(a) }));
    const int nearest = static_cast<int>(std::lround(tunerMidi));
    g.setFont(theme::mono(11.0f));
    g.drawText(noteName(nearest) + juce::String(nearest / 12 - 1) + " " + (tunerCents >= 0.0f ? "+" : "") + juce::String(juce::roundToInt(tunerCents)) + "c",
               juce::Rectangle<float>(90.0f, 14.0f).withCentre({ centre.x, centre.y - r - 14.0f }), juce::Justification::centred);
}

void SpiralView::paintTuner(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    auto box = juce::Rectangle<float>(radius * 0.78f, radius * 0.62f).withCentre(centre);
    g.setColour(theme::desk.withAlpha(0.9f));
    g.fillRoundedRectangle(box, 10.0f);
    if (! tunerValid)
    {
        g.setColour(theme::mute);
        g.setFont(theme::font(16.0f));
        g.drawText("play one note", box, juce::Justification::centred);
        return;
    }
    // Reference: the target note nearest to what is played, else the nearest note.
    int reference = static_cast<int>(std::lround(tunerMidi));
    if (settings.target >= 0)
    {
        const float base = std::floor((tunerMidi - static_cast<float>(settings.target)) / 12.0f + 0.5f) * 12.0f + static_cast<float>(settings.target);
        reference = static_cast<int>(base);
    }
    const float cents = (tunerMidi - static_cast<float>(reference)) * 100.0f;
    const auto colour = centsColour(cents);
    const int written = reference + settings.transpose;

    auto inner = box.reduced(10.0f);
    g.setColour(theme::ink);
    g.setFont(theme::font(juce::jlimit(18.0f, 54.0f, radius * 0.16f), true));
    g.drawText(noteName(written) + juce::String(static_cast<int>(std::floor(written / 12.0)) - 1), inner.removeFromTop(inner.getHeight() * 0.38f),
               juce::Justification::centred);
    g.setColour(colour);
    g.setFont(theme::mono(juce::jlimit(12.0f, 22.0f, radius * 0.07f), true));
    g.drawText((cents >= 0.0f ? "+" : "") + juce::String(cents, 1) + " c", inner.removeFromTop(inner.getHeight() * 0.3f), juce::Justification::centred);

    // Zoomed ruler: 7 semitones centred on the reference, the needle where the note is.
    auto ruler = inner.reduced(inner.getWidth() * 0.04f, 2.0f);
    const float semitoneW = ruler.getWidth() / 7.0f;
    const float midY = ruler.getY() + ruler.getHeight() * 0.35f;
    g.setColour(theme::line);
    g.fillRoundedRectangle(juce::Rectangle<float>(ruler.getX(), midY - 1.5f, ruler.getWidth(), 3.0f), 1.5f);
    g.setFont(theme::font(juce::jlimit(9.0f, 12.0f, semitoneW * 0.28f)));
    for (int k = -3; k <= 3; ++k)
    {
        const float x = ruler.getCentreX() + static_cast<float>(k) * semitoneW;
        const bool centreTick = k == 0;
        g.setColour(centreTick ? theme::ink : theme::mute);
        g.fillRect(juce::Rectangle<float>(centreTick ? 2.0f : 1.0f, centreTick ? 14.0f : 9.0f).withCentre({ x, midY }));
        // Quarter-tone ticks between semitones.
        if (k < 3)
            g.fillRect(juce::Rectangle<float>(1.0f, 4.0f).withCentre({ x + semitoneW * 0.5f, midY }));
        g.drawText(noteName(written + k), juce::Rectangle<float>(semitoneW, 14.0f).withCentre({ x, midY + 14.0f }), juce::Justification::centred);
    }
    const float offset = juce::jlimit(-3.5f, 3.5f, tunerMidi - static_cast<float>(reference));
    const float x = ruler.getCentreX() + offset * semitoneW;
    g.setColour(colour);
    g.fillRoundedRectangle(juce::Rectangle<float>(5.0f, 20.0f).withCentre({ x, midY }), 2.0f);
}
