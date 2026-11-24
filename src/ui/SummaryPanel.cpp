#include "SummaryPanel.h"

using namespace rk;

namespace
{
int keyForCamelot(int number, bool minor)
{
    for (int t = 0; t < 12; ++t)
        if (toMusicalKey(keyIndex(t, minor)).camelotNumber() == number)
            return keyIndex(t, minor);
    return -1;
}

const char* characteristicName(Mode m)
{
    switch (m)
    {
        case Mode::dorian:     return "major 6th";
        case Mode::phrygian:   return "minor 2nd";
        case Mode::lydian:     return "raised 4th";
        case Mode::mixolydian: return "minor 7th";
        case Mode::locrian:    return "diminished 5th";
        default:               return "";
    }
}

bool compatible(int a, int b)
{
    if (a < 0 || b < 0)
        return false;
    const int na = toMusicalKey(a).camelotNumber(), nb = toMusicalKey(b).camelotNumber();
    if (na == nb)
        return true; // same, or the relative
    return keyIsMinor(a) == keyIsMinor(b) && (na % 12 == (nb + 1) % 12 || nb % 12 == (na + 1) % 12);
}
} // namespace

// ================================================================================================================
void CamelotWheel::setKey(int keyIndex, int localKey)
{
    if (keyIndex != key || localKey != local)
    {
        key = keyIndex;
        local = localKey;
        repaint();
    }
}

void CamelotWheel::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float outer = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    if (outer < 30.0f)
        return;
    const auto c = b.getCentre();
    const float mid = outer * 0.68f, inner = outer * 0.38f;
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int ring = 0; ring < 2; ++ring) // 0: B (major, outside), 1: A (minor, inside)
        for (int n = 1; n <= 12; ++n)
        {
            const bool minor = ring == 1;
            const int k = keyForCamelot(n, minor);
            const float a0 = twoPi * ((n % 12) - 0.5f) / 12.0f, a1 = twoPi * ((n % 12) + 0.5f) / 12.0f;
            const float r0 = minor ? inner : mid, r1 = minor ? mid : outer;
            juce::Path seg;
            seg.addPieSegment(juce::Rectangle<float>(r1 * 2.0f, r1 * 2.0f).withCentre(c), a0, a1, r0 / r1);
            const bool isKey = k == key, isCompatible = ! isKey && compatible(k, key);
            g.setColour(theme::keyColour(k, isKey ? 0.95f : (isCompatible ? 0.42f : 0.12f)));
            g.fillPath(seg);
            g.setColour(theme::desk);
            g.strokePath(seg, juce::PathStrokeType(1.5f));
            if (k == local && local != key)
            {
                g.setColour(theme::live);
                g.strokePath(seg, juce::PathStrokeType(2.0f));
            }

            const float rm = (r0 + r1) * 0.5f, am = twoPi * (n % 12) / 12.0f;
            const juce::Point<float> p { c.x + rm * std::sin(am), c.y - rm * std::cos(am) };
            const float fs = juce::jlimit(8.0f, 13.0f, outer * 0.085f);
            g.setColour(isKey ? theme::desk : theme::ink.withAlpha(isCompatible ? 0.95f : 0.55f));
            g.setFont(theme::font(fs, isKey));
            g.drawText(juce::String(n) + (minor ? "A" : "B"), juce::Rectangle<float>(fs * 3.0f, fs * 1.2f).withCentre(p.translated(0.0f, -fs * 0.45f)),
                       juce::Justification::centred);
            g.setFont(theme::font(fs * 0.78f));
            g.drawText(keyShortName(k), juce::Rectangle<float>(fs * 3.0f, fs).withCentre(p.translated(0.0f, fs * 0.55f)), juce::Justification::centred);
        }

    g.setColour(theme::mute);
    g.setFont(theme::font(juce::jlimit(8.0f, 11.0f, outer * 0.07f)));
    g.drawText("CAMELOT", juce::Rectangle<float>(inner * 2.0f, 14.0f).withCentre(c), juce::Justification::centred);
}

// ================================================================================================================
SummaryPanel::SummaryPanel()
{
    addAndMakeVisible(wheel);
    copyButton.setTooltip("Copy the scale for Melodyne / Auto-Tune (name and notes)");
    copyButton.setColour(juce::TextButton::buttonColourId, theme::well);
    copyButton.setColour(juce::TextButton::textColourOffId, theme::ink);
    copyButton.onClick = [this]
    {
        if (data.summary.valid)
            juce::SystemClipboard::copyTextToClipboard(data.summary.tuningScale().clipboardText());
    };
    addChildComponent(copyButton);
}

void SummaryPanel::update(const Snapshot& s, ViewMode mode, const juce::String& newTrackName, int newTrackKey)
{
    data = s;
    viewMode = mode;
    trackName = newTrackName;
    trackKey = newTrackKey;
    copyButton.setVisible(data.summary.valid);
    wheel.setKey(data.summary.valid ? data.summary.displayCamelotKey() : -1, data.localKey);
    repaint();
}

void SummaryPanel::resized()
{
    auto r = getLocalBounds().reduced(16);
    textArea = r.removeFromTop(juce::jmin(r.getHeight(), 282));
    copyButton.setBounds(textArea.getRight() - 54, textArea.getY() + scaleRowY, 54, 19);
    r.removeFromTop(6);
    const int barsHeight = r.getHeight() > 260 ? 84 : (r.getHeight() > 120 ? 64 : 0);
    barsArea = r.removeFromTop(barsHeight);
    r.removeFromTop(8);
    const int side = juce::jmin(r.getWidth(), r.getHeight());
    wheel.setVisible(side >= 120);
    wheel.setBounds(r.withSizeKeepingCentre(side, side));
}

void SummaryPanel::paint(juce::Graphics& g)
{
    theme::drawPanel(g, getLocalBounds().toFloat());
    const auto& s = data.summary;
    auto r = textArea;

    g.setColour(theme::mute);
    g.setFont(theme::font(12.0f, true));
    auto header = r.removeFromTop(18);
    g.drawText("SONG KEY", header, juce::Justification::left);
    if (viewMode == ViewMode::lock)
    {
        g.setColour(theme::amber);
        g.drawText("LOCKED", header, juce::Justification::right);
    }
    else if (data.ara)
        g.drawText("ARA: " + juce::String(data.araSources) + " of " + juce::String(data.araSourcesTotal) + " clips analysed", header,
                   juce::Justification::right);
    else if (s.valid)
        g.drawText(juce::String(s.analysedBins) + (data.hostTimeline ? " beats" : " x 0.5 s") + " analysed", header, juce::Justification::right);

    if (! s.valid)
    {
        g.setColour(theme::ink.withAlpha(0.7f));
        g.setFont(theme::font(26.0f, true));
        g.drawText("Listening...", r.removeFromTop(60), juce::Justification::left);
        g.setColour(theme::mute);
        g.setFont(theme::font(13.0f));
        g.drawFittedText(data.hostTimeline ? "Play the song from the host. Each beat that plays is added to this instance's key map; "
                                             "looping a section replaces it rather than counting it twice."
                                           : "Play audio into the input. The key builds up from everything heard.",
                         r.removeFromTop(60), juce::Justification::topLeft, 3);
        return;
    }

    // Key name + Camelot badge.
    auto keyRow = r.removeFromTop(62);
    const int key = s.displayCamelotKey();
    auto badge = keyRow.removeFromRight(78).withSizeKeepingCentre(74, 46);
    g.setColour(theme::keyColour(key));
    g.fillRoundedRectangle(badge.toFloat(), 8.0f);
    g.setColour(theme::desk);
    g.setFont(theme::font(26.0f, true));
    g.drawText(camelotCode(key), badge, juce::Justification::centred);
    g.setColour(theme::ink);
    g.setFont(theme::font(juce::jlimit(22.0f, 40.0f, keyRow.getWidth() * 0.12f), true));
    g.drawFittedText(s.displayName(), keyRow, juce::Justification::centredLeft, 1, 0.7f);

    // Mode line.
    auto modeRow = r.removeFromTop(38);
    g.setFont(theme::font(13.0f));
    if (s.mode.modal)
    {
        g.setColour(theme::amber);
        g.drawFittedText("Mode: " + s.mode.name() + "  -  characteristic " + noteName(s.mode.characteristicPc) + " (" + characteristicName(s.mode.mode)
                             + ").  Same notes as " + keyLongName(s.mode.camelotKeyIndex()) + ", " + camelotCode(s.mode.camelotKeyIndex()) + ".",
                         modeRow, juce::Justification::topLeft, 2);
    }
    else if (s.mode.hint && s.mode.mode != Mode::ionian && s.mode.mode != Mode::aeolian)
    {
        g.setColour(theme::mute);
        g.drawFittedText("Leans " + s.mode.name() + " (not certain). Shown as " + keyLongName(s.majorMinorKey) + ".", modeRow, juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour(theme::mute);
        g.drawFittedText(juce::String(keyIsMinor(s.majorMinorKey) ? "Natural minor (Aeolian)" : "Major (Ionian)") + " - no other mode indicated.", modeRow,
                         juce::Justification::topLeft, 2);
    }

    // Confidence.
    auto confRow = r.removeFromTop(26);
    const float confidence = juce::jlimit(0.0f, 1.0f, static_cast<float>((s.r - s.r2) * 6.0 + juce::jmax(0.0, s.r - 0.5)));
    g.setColour(theme::mute);
    g.setFont(theme::font(12.0f));
    g.drawText("Confidence", confRow.removeFromLeft(80), juce::Justification::centredLeft);
    auto bar = confRow.reduced(0, 9).toFloat();
    g.setColour(theme::well);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(confidence > 0.6f ? theme::green : (confidence > 0.3f ? theme::amber : theme::red));
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * confidence), 3.0f);

    // Detail rows.
    const auto row = [&](const juce::String& label, const juce::String& value, juce::Colour colour)
    {
        auto line = r.removeFromTop(19);
        g.setColour(theme::mute);
        g.setFont(theme::font(12.0f));
        g.drawText(label, line.removeFromLeft(110), juce::Justification::centredLeft);
        g.setColour(colour);
        g.setFont(theme::font(13.0f, true));
        g.drawText(value, line, juce::Justification::centredLeft);
    };
    const auto scale = s.tuningScale();
    {
        auto line = r.removeFromTop(19);
        g.setColour(theme::mute);
        g.setFont(theme::font(12.0f));
        g.drawText("Scale", line.removeFromLeft(110), juce::Justification::centredLeft);
        line.removeFromRight(60); // the Copy button
        g.setColour(theme::amber);
        g.setFont(theme::font(13.0f, true));
        g.drawText(scale.name() + "   " + scale.spelledNotes().joinIntoString(" "), line, juce::Justification::centredLeft, true);
    }
    row("Relative", keyLongName(relativeKey(s.majorMinorKey)) + "  " + camelotCode(relativeKey(s.majorMinorKey)), theme::ink);
    // When the bass and chord roots overruled the profile match, the profile's answer is the alternative to show.
    if (s.majorMinorKey != s.key)
        row("Profile match", keyLongName(s.key) + "  " + camelotCode(s.key) + "  (r " + juce::String(s.r, 2) + "; bass and chords say "
                                 + noteName(keyTonic(s.majorMinorKey)) + ")", theme::ink);
    else
        row("Runner-up", keyLongName(s.runnerUp) + "  " + camelotCode(s.runnerUp) + "  (r " + juce::String(s.r, 2) + " vs " + juce::String(s.r2, 2) + ")", theme::ink);
    row("Tuning", (s.tuningCents >= 0.0 ? "+" : "") + juce::String(s.tuningCents, 1) + " cents  (A4 = " + juce::String(440.0 * std::pow(2.0, s.tuningCents / 1200.0), 1)
                      + " Hz)", std::abs(s.tuningCents) > 15.0 ? theme::amber : theme::ink);
    row("Heard now", keyLongName(data.localKey) + "  " + camelotCode(data.localKey) + "   chord " + theme::chordName(data.chord), theme::keyColour(data.localKey));
    row("Key changes", juce::String(s.keyChanges), s.keyChanges > 0 ? theme::amber : theme::ink);
    // The track name's Camelot label, as a cross-check (never overriding the analysis).
    if (trackKey >= 0)
    {
        const int shown = s.displayCamelotKey();
        const bool agrees = toMusicalKey(trackKey).camelotNumber() == toMusicalKey(shown).camelotNumber() && keyIsMinor(trackKey) == keyIsMinor(shown);
        row("Track name", camelotCode(trackKey) + " " + keyLongName(trackKey) + (agrees ? "  (agrees)" : "  (differs)") + "  \"" + trackName + "\"",
            agrees ? theme::green : theme::amber);
    }

    // Presence of each pitch class across the song, scale notes in the key colour.
    if (! barsArea.isEmpty())
    {
        auto bars = barsArea.toFloat();
        g.setColour(theme::mute);
        g.setFont(theme::font(11.0f, true));
        g.drawText("NOTES IN THE SONG", bars.removeFromTop(14.0f), juce::Justification::left);
        auto labels = bars.removeFromBottom(14.0f);
        std::array<bool, 12> inScale {};
        for (int pc : s.scale())
            inScale[static_cast<std::size_t>(pc)] = true;
        double maxShare = 0.0;
        for (auto v : s.presence)
            maxShare = std::max(maxShare, v);
        const float w = bars.getWidth() / 12.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float h = maxShare > 0.0 ? static_cast<float>(s.presence[static_cast<std::size_t>(pc)] / maxShare) * bars.getHeight() : 0.0f;
            auto col = juce::Rectangle<float>(bars.getX() + w * pc + 2.0f, bars.getBottom() - h, w - 4.0f, h);
            g.setColour(inScale[static_cast<std::size_t>(pc)] ? theme::keyColour(key, pc == s.tonic() ? 1.0f : 0.7f) : theme::mute.withAlpha(0.35f));
            g.fillRoundedRectangle(col, 2.0f);
            g.setColour(pc == s.tonic() ? theme::amber : theme::mute);
            g.setFont(theme::font(10.0f, pc == s.tonic()));
            g.drawText(noteName(pc), juce::Rectangle<float>(labels.getX() + w * pc, labels.getY(), w, labels.getHeight()), juce::Justification::centred);
        }
    }
}
