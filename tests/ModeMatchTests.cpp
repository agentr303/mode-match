// Mode Match tests (PLAN.md 4a / 4a-2). Console app: exits non-zero when any test fails.
#include <juce_core/juce_core.h>

#include "analysis/KeyAnalyzer.h"
#include "analysis/ModeDetector.h"
#include "analysis/SpiralAnalyzer.h"
#include "analysis/nuvo/KeyDetector.h"

namespace
{
using Buffer = std::vector<float>;

// A tone with four harmonics (1/h), 5 ms fades, added into `out` (NuvoDeck DspTests' addTone).
void addTone(Buffer& out, double rate, double frequency, float level, int from, int count, int harmonics = 4)
{
    const int fade = static_cast<int>(0.005 * rate);
    for (int h = 1; h <= harmonics; ++h)
    {
        const double f = frequency * h;
        if (f >= rate * 0.45)
            break;
        const double step = 2.0 * juce::MathConstants<double>::pi * f / rate;
        const double cr = std::cos(step), sr = std::sin(step);
        double c = 1.0, s = 0.0;
        const float a = level / static_cast<float>(h);
        for (int i = 0; i < count; ++i)
        {
            const float env = static_cast<float>(juce::jmin(1.0, juce::jmin(i, count - 1 - i) / static_cast<double>(fade)));
            out[static_cast<std::size_t>(from + i)] += a * env * static_cast<float>(s);
            const double nc = c * cr - s * sr;
            s = s * cr + c * sr;
            c = nc;
        }
    }
}

// NuvoDeck DspTests' keyLoop: 2 s, I IV V I (minor: i iv V i), a loud bass, a quiet tonic + fifth pad.
Buffer keyLoop(int tonic, bool minor, double cents, double rate)
{
    const int length = static_cast<int>(2.0 * rate);
    Buffer out(static_cast<std::size_t>(length), 0.0f);
    const double tune = std::pow(2.0, cents / 1200.0);
    const auto hz = [tune](int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0) * tune; };
    const int third = minor ? 3 : 4;
    const std::array<std::pair<int, int>, 4> chords {{ { 0, third }, { 5, minor ? 3 : 4 }, { 7, 4 }, { 0, third } }};
    const int chordLength = length / 4;
    const int base = 60 + tonic;
    for (int k = 0; k < 4; ++k)
    {
        const int root = base + chords[static_cast<std::size_t>(k)].first;
        const int from = k * chordLength;
        for (const int note : { root, root + chords[static_cast<std::size_t>(k)].second, root + 7 })
            addTone(out, rate, hz(note > 76 ? note - 12 : note), 0.12f, from, chordLength);
        addTone(out, rate, hz(36 + tonic + chords[static_cast<std::size_t>(k)].first), 0.35f, from, chordLength);
    }
    for (const int note : { 48 + tonic, 55 + tonic })
        addTone(out, rate, hz(note), 0.05f, 0, length);
    return out;
}

enum class KeyAnswer { exact, relative, fifth, parallel, semitone, other };

KeyAnswer sortKeyAnswer(int tonic, bool minor, int foundTonic, bool foundMinor)
{
    const auto camelotOf = [](int t, bool m) { return nevo::audio::MusicalKey { t, m }.camelotNumber(); };
    if (foundTonic == tonic && foundMinor == minor)
        return KeyAnswer::exact;
    const int number = camelotOf(tonic, minor), found = camelotOf(foundTonic, foundMinor);
    if (found == number)
        return KeyAnswer::relative;
    if (foundMinor == minor && (found % 12 == (number + 1) % 12 || (found + 1) % 12 == number % 12))
        return KeyAnswer::fifth;
    if (foundTonic == tonic)
        return KeyAnswer::parallel;
    for (const int shift : { 1, 11 })
        if (camelotOf((foundTonic + shift) % 12, foundMinor) == number)
            return KeyAnswer::semitone;
    return KeyAnswer::other;
}

// Streams a buffer through KeyAnalyzer in host-sized blocks, without a timeline (free-running bins).
rk::KeySummary streamSummary(const Buffer& audio, double rate, int blockSize = 512)
{
    rk::KeyAnalyzer analyzer;
    analyzer.prepare(rate);
    rk::HostPosition none;
    for (int i = 0; i < static_cast<int>(audio.size()); i += blockSize)
    {
        const int n = juce::jmin(blockSize, static_cast<int>(audio.size()) - i);
        double energy = 0.0;
        for (int k = 0; k < n; ++k)
            energy += static_cast<double>(audio[static_cast<std::size_t>(i + k)]) * audio[static_cast<std::size_t>(i + k)];
        analyzer.process(audio.data() + i, n, none, energy, 1, true);
    }
    analyzer.finish();
    return analyzer.computeSummary();
}

// As a DAW plays it: the host timeline at `bpm` from beat 0, transport running, one bin per beat.
rk::KeySummary hostSummary(const Buffer& audio, double rate, double bpm, int blockSize = 512)
{
    rk::KeyAnalyzer analyzer;
    analyzer.prepare(rate);
    for (int i = 0; i < static_cast<int>(audio.size()); i += blockSize)
    {
        const int n = juce::jmin(blockSize, static_cast<int>(audio.size()) - i);
        rk::HostPosition pos;
        pos.hasTimeline = true;
        pos.playing = true;
        pos.bpm = bpm;
        pos.seconds = i / rate;
        pos.ppq = pos.seconds * bpm / 60.0;
        double energy = 0.0;
        for (int k = 0; k < n; ++k)
            energy += static_cast<double>(audio[static_cast<std::size_t>(i + k)]) * audio[static_cast<std::size_t>(i + k)];
        analyzer.process(audio.data() + i, n, pos, energy * 2.0, 2, true);
    }
    return analyzer.computeSummary();
}

KeyDetector::KeyResult nuvoDetect(const Buffer& audio, double rate)
{
    KeyDetector detector;
    const float* channels[1] = { audio.data() };
    return detector.detectKey(channels, static_cast<int>(audio.size()), 1, rate);
}

// Real recordings: decoded by ffmpeg (on PATH) to 44.1 kHz mono float. The songs are not shipped; a missing file or
// ffmpeg skips the check. Returns an empty buffer on failure.
constexpr double realSongRate = 44100.0;
Buffer decodeWithFfmpeg(const juce::File& file, double rate = realSongRate)
{
    if (! file.existsAsFile())
        return {};
    const auto raw = juce::File::createTempFile(".f32");
    juce::ChildProcess ffmpeg;
    const juce::StringArray args { "ffmpeg", "-v", "error", "-y", "-i", file.getFullPathName(), "-ac", "1",
                                   "-ar", juce::String(juce::roundToInt(rate)), "-f", "f32le", raw.getFullPathName() };
    if (! ffmpeg.start(args, 0) || ! ffmpeg.waitForProcessToFinish(300000) || ffmpeg.getExitCode() != 0)
        return {};
    juce::MemoryBlock data;
    raw.loadFileAsData(data);
    raw.deleteFile();
    Buffer out(data.getSize() / sizeof(float));
    std::memcpy(out.data(), data.getData(), out.size() * sizeof(float));
    return out;
}

juce::String chromaText(const rk::Chroma& c)
{
    juce::String s;
    for (int pc = 0; pc < 12; ++pc)
        s << rk::noteName(pc) << ":" << juce::String(c[static_cast<std::size_t>(pc)], 2) << " ";
    return s.trimEnd();
}

rk::Chroma normalised(rk::Chroma c)
{
    double m = 0.0;
    for (auto v : c)
        m = std::max(m, v);
    if (m > 0.0)
        for (auto& v : c)
            v /= m;
    return c;
}

juce::String tonicScoreText(const rk::ModeResult& m)
{
    static const int steps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    juce::String t;
    for (int d = 0; d < 7; ++d)
        t << rk::noteName((m.collection + steps[d]) % 12) << ":" << juce::String(m.tonicScores[static_cast<std::size_t>(d)], 3) << " ";
    return t.trimEnd();
}

void printSummary(const rk::KeySummary& s)
{
    std::cout << "display     " << s.displayName() << " (" << rk::camelotCode(s.displayCamelotKey()) << ")\n"
              << "nuvo key    " << rk::keyLongName(s.key) << " r=" << s.r << "  runner-up " << rk::keyLongName(s.runnerUp)
              << " r=" << s.r2 << "\n"
              << "mode stage  " << s.mode.name() << " collection " << rk::noteName(s.mode.collection) << " margin "
              << s.mode.margin << (s.mode.modal ? " MODAL" : "") << (s.mode.hint ? " hint" : "") << "\n"
              << "tonic score " << tonicScoreText(s.mode) << "\n"
              << "tuning      " << s.tuningCents << " cents\n"
              << "aggregate   " << chromaText(s.aggregate) << "\n"
              << "presence    " << chromaText(s.presence) << "\n"
              << "bass        " << chromaText(normalised(s.bass)) << "\n"
              << "chord roots " << chromaText(normalised(s.chordRoots)) << std::endl;
    std::cout << "shown key   " << rk::keyLongName(s.majorMinorKey) << std::endl;
}
} // namespace

// ================================================================================================================
class KeyEngineTests final : public juce::UnitTest
{
public:
    KeyEngineTests() : juce::UnitTest("Key engine (NuvoDeck port)", "ModeMatch") {}

    void runTest() override
    {
        struct Counts
        {
            std::array<int, 6> n {};
            int total = 0;
        };
        Counts nuvo, stream, shown;
        int identical = 0;
        juce::StringArray differences;

        beginTest("240 synthesised loops: NuvoDeck KeyDetector (the copied header) and the streaming KeyAnalyzer");
        for (const double rate : { 44100.0, 48000.0 })
            for (const double cents : { -45.0, -25.0, 0.0, 25.0, 45.0 })
                for (const bool minor : { false, true })
                    for (int tonic = 0; tonic < 12; ++tonic)
                    {
                        const auto loop = keyLoop(tonic, minor, cents, rate);
                        const auto a = nuvoDetect(loop, rate);
                        const auto s = streamSummary(loop, rate);
                        ++nuvo.n[static_cast<std::size_t>(sortKeyAnswer(tonic, minor, a.rootNote, a.isMinor))];
                        ++nuvo.total;
                        const int st = s.valid ? rk::keyTonic(s.key) : -1;
                        const bool sm = s.valid && rk::keyIsMinor(s.key);
                        ++stream.n[static_cast<std::size_t>(s.valid ? sortKeyAnswer(tonic, minor, st, sm) : KeyAnswer::other)];
                        ++stream.total;
                        const int dt = s.valid ? rk::keyTonic(s.majorMinorKey) : -1;
                        const bool dm = s.valid && rk::keyIsMinor(s.majorMinorKey);
                        ++shown.n[static_cast<std::size_t>(s.valid ? sortKeyAnswer(tonic, minor, dt, dm) : KeyAnswer::other)];
                        ++shown.total;
                        if (s.valid && st == a.rootNote && sm == a.isMinor)
                            ++identical;
                        else
                            differences.add(rk::keyShortName(rk::keyIndex(tonic, minor)) + " " + juce::String(cents, 0) + "c "
                                            + juce::String(rate / 1000.0, 1) + "k: NuvoDeck " + a.keyName + ", stream " + rk::keyShortName(s.key));
                    }

        const auto report = [this](const char* who, const Counts& c)
        {
            logMessage(juce::String(who) + ": exact " + juce::String(c.n[0]) + ", relative " + juce::String(c.n[1]) + ", fifth " + juce::String(c.n[2])
                       + ", parallel " + juce::String(c.n[3]) + ", semitone " + juce::String(c.n[4]) + ", other " + juce::String(c.n[5]) + " of " + juce::String(c.total));
        };
        report("NuvoDeck KeyDetector", nuvo);
        report("Streaming KeyAnalyzer", stream);
        report("Shown key (bass/chord tonic)", shown);
        logMessage("Identical answers: " + juce::String(identical) + " of " + juce::String(stream.total));
        for (const auto& d : differences)
            logMessage("  " + d);

        expectEquals(nuvo.n[4], 0, "NuvoDeck: no semitone errors");
        expectEquals(nuvo.n[5], 0, "NuvoDeck: nothing far off");
        expectGreaterOrEqual(nuvo.n[0], nuvo.total * 95 / 100, "NuvoDeck: at least 95 % exact");

        beginTest("Streaming port meets NuvoDeck's own criteria and agrees with it");
        expectEquals(stream.n[4], 0, "stream: no semitone errors");
        expectEquals(stream.n[5], 0, "stream: nothing far off");
        expectGreaterOrEqual(stream.n[0], stream.total * 95 / 100, "stream: at least 95 % exact");
        expectGreaterOrEqual(identical, stream.total * 97 / 100, "stream: the same key as KeyDetector in at least 97 % of loops");

        beginTest("The shown key (bass / chord-root tonic over the profile match) loses nothing on the 240 loops");
        expectEquals(shown.n[4], 0, "shown: no semitone errors");
        expectEquals(shown.n[5], 0, "shown: nothing far off");
        expectGreaterOrEqual(shown.n[0], stream.n[0], "shown: at least as many exact as the profile match");

        beginTest("Longer audio (several 30 s blocks): same key as KeyDetector");
        {
            const double rate = 44100.0;
            Buffer song;
            // 70 s: A minor loops, louder in the middle (the "drop") - exercises blocks, loudness^2 and the partial block.
            for (int i = 0; i < 35; ++i)
            {
                auto loop = keyLoop(9, true, 0.0, rate);
                const float gain = (i >= 12 && i < 24) ? 1.0f : 0.5f;
                for (auto& v : loop)
                    v *= gain;
                song.insert(song.end(), loop.begin(), loop.end());
            }
            const auto a = nuvoDetect(song, rate);
            const auto s = streamSummary(song, rate, 1024);
            expect(s.valid, "summary valid");
            expectEquals(rk::keyShortName(s.key), a.keyName.replace("A#", "Bb").replace("C#", "Db").replace("D#", "Eb").replace("G#", "Ab"),
                         "same key as KeyDetector on 70 s");
            expectEquals(rk::keyShortName(s.key), juce::String("Am"));
        }
    }
};

// ================================================================================================================
class TimelineTests final : public juce::UnitTest
{
public:
    TimelineTests() : juce::UnitTest("Timeline map", "ModeMatch") {}

    void runTest() override
    {
        const double rate = 48000.0, bpm = 120.0;
        // 16 beats (8 s) of audio: 4 loops in A minor.
        Buffer section;
        for (int i = 0; i < 4; ++i)
        {
            auto l = keyLoop(9, true, 0.0, rate);
            section.insert(section.end(), l.begin(), l.end());
        }

        const auto run = [&](int passes)
        {
            rk::KeyAnalyzer a;
            a.prepare(rate);
            const int block = 512;
            for (int p = 0; p < passes; ++p)
                for (int i = 0; i < static_cast<int>(section.size()); i += block)
                {
                    rk::HostPosition pos;
                    pos.hasTimeline = true;
                    pos.playing = true;
                    pos.bpm = bpm;
                    pos.seconds = i / rate;
                    pos.ppq = pos.seconds * bpm / 60.0; // a loop: the position jumps back to 0 each pass
                    const int n = juce::jmin(block, static_cast<int>(section.size()) - i);
                    double e = 0.0;
                    for (int k = 0; k < n; ++k)
                        e += section[static_cast<std::size_t>(i + k)] * section[static_cast<std::size_t>(i + k)];
                    a.process(section.data() + i, n, pos, e, 1, true);
                }
            return a;
        };

        beginTest("Looping a section three times replaces its bins instead of adding to them");
        {
            const auto once = run(1);
            const auto thrice = run(3);
            double w1 = 0.0, w3 = 0.0;
            for (const auto& [i, b] : once.bins())
                w1 += b.weight;
            for (const auto& [i, b] : thrice.bins())
                w3 += b.weight;
            logMessage("total weight: one pass " + juce::String(w1, 1) + ", three passes " + juce::String(w3, 1));
            expectWithinAbsoluteError(w3 / w1, 1.0, 0.15, "three passes weigh about the same as one (not three times)");
            expect(once.bins().size() == thrice.bins().size(), "same bins");
            const auto s1 = once.computeSummary(), s3 = thrice.computeSummary();
            expectEquals(s3.key, s1.key, "same key");
            for (int pc = 0; pc < 12; ++pc)
                expectWithinAbsoluteError(s3.presence[static_cast<std::size_t>(pc)], s1.presence[static_cast<std::size_t>(pc)], 0.02, "presence within 2 %");
            expectEquals(rk::keyShortName(s1.key), juce::String("Am"));
        }

        beginTest("Stopped transport: nothing is filed");
        {
            rk::KeyAnalyzer a;
            a.prepare(rate);
            rk::HostPosition pos;
            pos.hasTimeline = true;
            pos.playing = false;
            for (int i = 0; i + 512 <= static_cast<int>(section.size()); i += 512)
                a.process(section.data() + i, 512, pos, 0.0, 1, true);
            expect(a.bins().empty(), "no bins while stopped");
            expect(a.localKey() >= 0, "the local key still follows the audio");
        }

        beginTest("State round trip");
        {
            const auto a = run(1);
            juce::MemoryOutputStream out;
            a.writeTo(out);
            rk::KeyAnalyzer b;
            b.prepare(rate);
            juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
            expect(b.readFrom(in), "reads back");
            expectEquals(b.computeSummary().key, a.computeSummary().key, "same key after reload");
        }
    }
};

// ================================================================================================================
// The tonic from the bass: a dance track whose root sits below KeyDetector's 100 Hz band.
class BassTonicTests final : public juce::UnitTest
{
public:
    BassTonicTests() : juce::UnitTest("Bass tonic", "ModeMatch") {}

    // 32 s of F minor: an F bass at F1/F2 (43.7 / 87.3 Hz, both under 100 Hz) for the whole bar, a pad of Ab3 C4 Eb4
    // (an Fm7 without its root above 100 Hz), and a melody walking G Bb Db so the Ab-major collection is complete. The
    // profile match alone sees Ab-C-Eb and answers Ab major or C minor.
    static Buffer fMinorOverLowBass(double rate)
    {
        const int bar = static_cast<int>(2.0 * rate);
        Buffer out(static_cast<std::size_t>(bar * 16), 0.0f);
        const auto hz = [](int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); };
        const int melody[8] = { 67, 70, 72, 73, 72, 70, 68, 67 }; // G Bb C Db C Bb Ab G
        for (int b = 0; b < 16; ++b)
        {
            const int from = b * bar;
            addTone(out, rate, hz(29), 0.40f, from, bar, 2); // F1
            addTone(out, rate, hz(41), 0.25f, from, bar, 2); // F2
            for (const int note : { 56, 60, 63 })           // Ab3 C4 Eb4
                addTone(out, rate, hz(note), 0.14f, from, bar);
            for (int n = 0; n < 4; ++n)
                addTone(out, rate, hz(melody[(b * 4 + n) % 8]), 0.05f, from + n * bar / 4, bar / 4);
        }
        return out;
    }

    void runTest() override
    {
        beginTest("F minor whose root is only in the bass (under 100 Hz) shows F minor");
        {
            const auto s = streamSummary(fMinorOverLowBass(44100.0), 44100.0, 1024);
            expect(s.valid, "summary valid");
            expectEquals(s.displayName(), juce::String("F minor"),
                         "shown key (profile match was " + rk::keyLongName(s.key) + ", mode-stage margin " + juce::String(s.mode.margin, 3) + ")");
            expectEquals(rk::camelotCode(s.displayCamelotKey()), juce::String("4A"));
            if (s.displayName() != "F minor")
                printSummary(s);
        }

        // Real recordings, checked when present (not shipped): file name in the project folder, expected key.
        // Mixed In Key reads "Prophets of Hope" as F minor at 96 %; the bass sits on F throughout.
        struct RealSong { const char* file; const char* expected; };
        static const RealSong songs[] = { { "Nora En Pure  - Prophets of Hope-1.mp3", "F minor" } };
        const juce::File root(MM_SOURCE_DIR);
        for (const auto& song : songs)
        {
            const auto audio = decodeWithFfmpeg(root.getChildFile(song.file));
            if (audio.empty())
            {
                logMessage(juce::String("  skipped (file or ffmpeg missing): ") + song.file);
                continue;
            }
            beginTest(juce::String("Real song: ") + song.file);
            const auto s = streamSummary(audio, realSongRate, 1024);
            expectEquals(s.displayName(), juce::String(song.expected), "profile match was " + rk::keyLongName(s.key));
        }
    }
};

// ================================================================================================================
class ModeTests final : public juce::UnitTest
{
public:
    ModeTests() : juce::UnitTest("Mode stage", "ModeMatch") {}

    // A 16 s vamp in a mode. Bass on each chord root, a quiet drone on the tonic.
    static Buffer modeVamp(int tonic, rk::Mode mode, double rate)
    {
        const auto iv = rk::modeIntervals(mode);
        const auto triad = [&](int degree) {
            return std::array<int, 3> { iv[static_cast<std::size_t>(degree % 7)] + (degree >= 7 ? 12 : 0),
                                        iv[static_cast<std::size_t>((degree + 2) % 7)] + ((degree + 2) >= 7 ? 12 : 0),
                                        iv[static_cast<std::size_t>((degree + 4) % 7)] + ((degree + 4) >= 7 ? 12 : 0) };
        };
        int colour = 4, other = 3; // scale degrees (0-based) of the two non-tonic chords
        switch (mode)
        {
            case rk::Mode::ionian:     colour = 4; other = 3; break; // V, IV
            case rk::Mode::dorian:     colour = 3; other = 6; break; // IV (major 6th), bVII
            case rk::Mode::phrygian:   colour = 1; other = 6; break; // bII, bvii
            case rk::Mode::lydian:     colour = 1; other = 4; break; // II (raised 4th), V
            case rk::Mode::mixolydian: colour = 6; other = 3; break; // bVII, IV
            case rk::Mode::aeolian:    colour = 3; other = 4; break; // iv, v (i iv bVI never plays the 2nd: Aeolian or Phrygian?)
            case rk::Mode::locrian:    colour = 1; other = 5; break; // bII, bvi
        }
        const int length = static_cast<int>(16.0 * rate);
        Buffer out(static_cast<std::size_t>(length), 0.0f);
        const auto hz = [](double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); };
        // Modal vamps alternate the tonic and the colour chord (Dm7-G in Dorian): tonic 4 s, colour 4 s, tonic 4 s,
        // another diatonic chord 2 s, tonic 2 s.
        const struct { int degree; double from, len; } plan[] = { { 0, 0, 4 }, { colour, 4, 4 }, { 0, 8, 4 }, { other, 12, 2 }, { 0, 14, 2 } };
        for (const auto& p : plan)
        {
            const int from = static_cast<int>(p.from * rate), count = static_cast<int>(p.len * rate);
            for (int n : triad(p.degree))
            {
                int midi = 60 + tonic + n;
                while (midi > 76)
                    midi -= 12;
                addTone(out, rate, hz(midi), 0.12f, from, count);
            }
            const int rootPc = (tonic + iv[static_cast<std::size_t>(p.degree % 7)]) % 12;
            addTone(out, rate, hz(36 + rootPc), 0.30f, from, count); // bass, octave 2
        }
        addTone(out, rate, hz(48 + tonic % 12), 0.04f, 0, length); // a quiet drone on the tonic
        return out;
    }

    void runTest() override
    {
        beginTest("Synthesised vamps: 7 modes x 12 tonics");
        const double rate = 44100.0;
        int collectionRight = 0, modeRight = 0, total = 0, falseModal = 0, majorMinorRight = 0, majorMinorTotal = 0;
        juce::StringArray misses;
        for (int m = 0; m < 7; ++m)
            for (int tonic = 0; tonic < 12; ++tonic)
            {
                const auto mode = static_cast<rk::Mode>(m);
                const auto s = streamSummary(modeVamp(tonic, mode, rate), rate, 1024);
                ++total;
                const int expectedCollection = (tonic - rk::modeIntervals(rk::Mode::ionian)[static_cast<std::size_t>(m)] + 12) % 12;
                if (s.mode.collection == expectedCollection)
                    ++collectionRight;
                else
                    misses.add("collection: " + rk::noteName(tonic) + " " + rk::modeName(mode) + " -> stage " + rk::noteName(s.mode.tonic) + " "
                               + rk::modeName(s.mode.mode) + " (collection " + rk::noteName(s.mode.collection) + ", expected "
                               + rk::noteName(expectedCollection) + ")");
                const bool plain = mode == rk::Mode::ionian || mode == rk::Mode::aeolian;
                bool right;
                if (plain)
                {
                    ++majorMinorTotal;
                    right = ! s.mode.modal && s.key == rk::keyIndex(tonic, mode == rk::Mode::aeolian);
                    if (s.mode.modal)
                        ++falseModal;
                    if (right)
                        ++majorMinorRight;
                }
                else
                    right = s.mode.modal && s.mode.mode == mode && s.mode.tonic == tonic;
                if (right)
                    ++modeRight;
                else
                    misses.add(rk::noteName(tonic) + " " + rk::modeName(mode) + " -> " + (s.mode.modal ? s.mode.name() : rk::keyLongName(s.key))
                               + " (stage " + rk::noteName(s.mode.tonic) + " " + rk::modeName(s.mode.mode) + ", margin " + juce::String(s.mode.margin, 3)
                               + ", char " + juce::String(s.mode.characteristicShare * 100.0, 1) + "%, repl " + juce::String(s.mode.replacedShare * 100.0, 1) + "%)"
                               + (tonic == 0 ? " presence " + [&s] { juce::String p; for (int i = 0; i < 12; ++i) p << rk::noteName(i) << ":" << juce::String(s.presence[static_cast<std::size_t>(i)] * 100.0, 1) << " "; return p; }() : juce::String()));
            }
        logMessage("collection right " + juce::String(collectionRight) + "/" + juce::String(total) + ", mode right " + juce::String(modeRight) + "/"
                   + juce::String(total) + ", major/minor right " + juce::String(majorMinorRight) + "/" + juce::String(majorMinorTotal)
                   + ", major/minor reported as another mode " + juce::String(falseModal));
        for (const auto& line : misses)
            logMessage("  " + line);
        expectEquals(collectionRight, total, "the pitch collection, always");
        expectGreaterOrEqual(modeRight, total * 85 / 100, "the mode, at least 85 %");
        expectEquals(falseModal, 0, "major / minor songs never reported as another mode");

        beginTest("The 240 major/minor loops are never reported as another mode");
        int modal = 0, loops = 0;
        for (const bool minor : { false, true })
            for (int tonic = 0; tonic < 12; ++tonic)
                for (const double cents : { -25.0, 0.0, 25.0 })
                {
                    const auto s = streamSummary(keyLoop(tonic, minor, cents, 48000.0), 48000.0);
                    ++loops;
                    if (s.mode.modal)
                        ++modal;
                }
        logMessage(juce::String(modal) + " of " + juce::String(loops) + " major/minor loops named as a mode");
        expectEquals(modal, 0, "no false modes");

        beginTest("Camelot codes and mode display codes");
        expectEquals(rk::camelotCode(rk::keyIndex(0, false)), juce::String("8B"));
        expectEquals(rk::camelotCode(rk::keyIndex(9, true)), juce::String("8A"));
        expectEquals(rk::camelotCode(rk::keyIndex(11, true)), juce::String("10A"));
        expectEquals(rk::camelotCode(rk::keyIndex(7, false)), juce::String("9B"));
        rk::ModeResult dorian;
        dorian.modal = true;
        dorian.mode = rk::Mode::dorian;
        dorian.tonic = 2;
        dorian.collection = 0;
        expectEquals(rk::camelotCode(dorian.camelotKeyIndex()), juce::String("8A"), "D Dorian shows 8A (same notes as A minor)");
        rk::ModeResult mixo = dorian;
        mixo.mode = rk::Mode::mixolydian;
        mixo.tonic = 7;
        expectEquals(rk::camelotCode(mixo.camelotKeyIndex()), juce::String("8B"), "G Mixolydian shows 8B (same notes as C major)");
        expectEquals(dorian.name(), juce::String("D Dorian"));
    }
};

// ================================================================================================================
class SpiralTests final : public juce::UnitTest
{
public:
    SpiralTests() : juce::UnitTest("Spiral engine", "ModeMatch") {}

    void runTest() override
    {
        beginTest("A4 at +3 cents: a partial and the YIN pitch within 1 cent");
        const double rate = 48000.0;
        Buffer tone(static_cast<std::size_t>(rate), 0.0f);
        addTone(tone, rate, 440.0 * std::pow(2.0, 3.0 / 1200.0), 0.5f, 0, static_cast<int>(tone.size()));
        rk::SpiralAnalyzer s;
        s.prepare(rate);
        s.push(tone.data(), static_cast<int>(tone.size()));
        bool found = false;
        for (const auto& p : s.partials())
            if (std::abs(p.midi - 69.03f) < 0.01f)
                found = true;
        expect(found, "a partial at 69.03 (+-1 cent)");
        expect(s.pitch().valid, "pitch found");
        expectWithinAbsoluteError(s.pitch().midi, 69.03f, 0.01f, "YIN within 1 cent");
        logMessage("strongest partial " + juce::String(s.partials().empty() ? 0.0f : s.partials().front().midi, 4) + ", YIN " + juce::String(s.pitch().midi, 4));

        beginTest("E2 (82.4 Hz) resolved by the long frame within 2 cents");
        Buffer low(static_cast<std::size_t>(rate), 0.0f);
        addTone(low, rate, 82.4069, 0.5f, 0, static_cast<int>(low.size()));
        rk::SpiralAnalyzer s2;
        s2.prepare(rate);
        s2.push(low.data(), static_cast<int>(low.size()));
        bool lowFound = false;
        for (const auto& p : s2.partials())
            if (std::abs(p.midi - 40.0f) < 0.02f)
                lowFound = true;
        expect(lowFound, "a partial at 40.00 (+-2 cents)");

        beginTest("Every window: 300 Hz, 1 kHz and 3 kHz tones within 1 cent (8192, 4096 and 2048-point frames)");
        for (const double f : { 300.0, 1000.0, 3000.0 })
        {
            Buffer t(static_cast<std::size_t>(rate), 0.0f);
            addTone(t, rate, f, 0.5f, 0, static_cast<int>(t.size()), 1);
            rk::SpiralAnalyzer sa;
            sa.prepare(rate);
            sa.push(t.data(), static_cast<int>(t.size()));
            const float expected = static_cast<float>(69.0 + 12.0 * std::log2(f / 440.0));
            float closest = 99.0f;
            int near = 0;
            for (const auto& p : sa.partials())
            {
                closest = std::min(closest, std::abs(p.midi - expected));
                if (std::abs(p.midi - expected) < 0.5f)
                    ++near;
            }
            expectLessThan(closest, 0.01f, juce::String(f) + " Hz within 1 cent");
            expectEquals(near, 1, juce::String(f) + " Hz: one partial, not one per window");
        }
    }
};

// ================================================================================================================
// Chord qualities, HMM smoothing, the percussion mask, bar positions, phrase evidence and the tuning scale.
class AnalysisExtrasTests final : public juce::UnitTest
{
public:
    AnalysisExtrasTests() : juce::UnitTest("Chords, smoothing and scales", "ModeMatch") {}

    static double hz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

    // Streams audio without a timeline and returns the analyser (for its current chord and local key).
    static void feed(rk::KeyAnalyzer& a, const Buffer& audio, double)
    {
        rk::HostPosition none;
        for (int i = 0; i < static_cast<int>(audio.size()); i += 512)
        {
            const int n = juce::jmin(512, static_cast<int>(audio.size()) - i);
            double e = 0.0;
            for (int k = 0; k < n; ++k)
                e += static_cast<double>(audio[static_cast<std::size_t>(i + k)]) * audio[static_cast<std::size_t>(i + k)];
            a.process(audio.data() + i, n, none, e, 1, true);
        }
    }

    void runTest() override
    {
        const double rate = 48000.0;

        beginTest("Chord qualities: major, minor, 7, diminished, sus4");
        {
            struct Case { rk::ChordQuality q; std::array<int, 4> steps; int count; };
            const Case cases[] = { { rk::ChordQuality::major, { 0, 4, 7, 0 }, 3 },
                                   { rk::ChordQuality::minor, { 0, 3, 7, 0 }, 3 },
                                   { rk::ChordQuality::dominant7, { 0, 4, 7, 10 }, 4 },
                                   { rk::ChordQuality::diminished, { 0, 3, 6, 0 }, 3 },
                                   { rk::ChordQuality::sus4, { 0, 5, 7, 0 }, 3 } };
            for (const int root : { 0, 5, 10 })
                for (const auto& c : cases)
                {
                    Buffer audio(static_cast<std::size_t>(3.0 * rate), 0.0f);
                    for (int i = 0; i < c.count; ++i)
                        addTone(audio, rate, hz(60 + root + c.steps[static_cast<std::size_t>(i)]), 0.12f, 0, static_cast<int>(audio.size()));
                    addTone(audio, rate, hz(36 + root), 0.3f, 0, static_cast<int>(audio.size()));
                    rk::KeyAnalyzer a;
                    a.prepare(rate);
                    feed(a, audio, rate);
                    const int expected = rk::chordIndex(root, c.q);
                    expectEquals(rk::chordName(a.currentChord()), rk::chordName(expected));
                }
        }

        beginTest("Local key HMM: a modulation from C major to Eb major is followed, once");
        {
            Buffer audio;
            for (int i = 0; i < 12; ++i)
            {
                const auto l = keyLoop(i < 6 ? 0 : 3, false, 0.0, rate); // 12 s of C major, then 12 s of Eb major
                audio.insert(audio.end(), l.begin(), l.end());
            }
            const auto s = hostSummary(audio, rate, 120.0);
            rk::KeyAnalyzer a;
            a.prepare(rate);
            feed(a, audio, rate);
            expectEquals(rk::keyLongName(a.localKey()), juce::String("Eb major"), "local key at the end");
            expectEquals(s.keyChanges, 1, "one key change on the key map");
        }

        beginTest("OnlineViterbi: a new state takes over once its evidence outweighs the switch cost");
        {
            rk::OnlineViterbi v;
            v.reset(2);
            int switchedAt = -1;
            for (int t = 0; t < 40; ++t)
            {
                const int obs = t < 10 ? 0 : 1;
                const int s = v.step([obs](int st) { return st == obs ? 0.0 : -1.0; }, [](int, int) { return 5.0; });
                if (s == 1 && switchedAt < 0)
                    switchedAt = t;
            }
            expectEquals(switchedAt, 15, "switch after 5 frames of the new state's evidence (cost 5, 1 per frame)");
        }

        beginTest("Percussion mask: noise bursts on every 8th note do not move the key or the chords");
        {
            Buffer audio;
            for (int i = 0; i < 8; ++i)
            {
                const auto l = keyLoop(7, false, 0.0, rate); // G major
                audio.insert(audio.end(), l.begin(), l.end());
            }
            juce::Random random(42);
            const int step = static_cast<int>(0.25 * rate), burst = static_cast<int>(0.05 * rate);
            for (int start = 0; start + burst < static_cast<int>(audio.size()); start += step)
                for (int i = 0; i < burst; ++i)
                    audio[static_cast<std::size_t>(start + i)] += 0.6f * (random.nextFloat() * 2.0f - 1.0f) * (1.0f - static_cast<float>(i) / burst);
            rk::KeyAnalyzer a;
            a.prepare(rate);
            feed(a, audio, rate);
            expectEquals(rk::keyLongName(a.localKey()), juce::String("G major"), "local key");
            const auto s = a.computeSummary();
            expectEquals(s.displayName(), juce::String("G major"), "song key");
            int inKey = 0, total = 0;
            for (const auto& [i, b] : a.bins())
                if (b.chord >= 0)
                {
                    ++total;
                    const int root = rk::chordRoot(b.chord);
                    if (root == 7 || root == 0 || root == 2)
                        ++inKey;
                }
            logMessage("chords on I / IV / V: " + juce::String(inKey) + " of " + juce::String(total));
            expectGreaterOrEqual(inKey, total * 9 / 10, "at least 90 % of the chords are G, C or D");
        }

        beginTest("Bars from the host's bar lines (3/4, first bar line at beat 1)");
        {
            const auto loop = keyLoop(0, false, 0.0, rate);
            rk::KeyAnalyzer a;
            a.prepare(rate);
            const double bpm = 120.0;
            for (int i = 0; i < static_cast<int>(loop.size()); i += 512)
            {
                const int n = juce::jmin(512, static_cast<int>(loop.size()) - i);
                rk::HostPosition pos;
                pos.hasTimeline = pos.playing = true;
                pos.bpm = bpm;
                pos.seconds = i / rate;
                pos.ppq = 1.0 + pos.seconds * bpm / 60.0;
                pos.beatsPerBar = 3;
                pos.hasBarStart = true;
                pos.barStartPpq = 1.0 + 3.0 * std::floor((pos.ppq - 1.0) / 3.0);
                a.process(loop.data() + i, n, pos, 1.0, 1, true);
            }
            bool allRight = true;
            for (const auto& [index, b] : a.bins())
                if (b.weight > 0.0f)
                    allRight = allRight && b.beatsPerBar == 3 && b.barBeat == (index - 1) % 3;
            expect(allRight, "beat in bar counted from the bar line");
        }

        beginTest("Phrase evidence breaks a tie between two tonics; without it the old weights stand");
        {
            rk::ModeInputs in;
            for (int pc : { 0, 2, 4, 5, 7, 9, 11 })
                in.chroma[static_cast<std::size_t>(pc)] = in.presence[static_cast<std::size_t>(pc)] = 1.0 / 7.0;
            in.bass[0] = in.bass[9] = 1.0;
            in.chordRoots[0] = in.chordRoots[9] = 1.0;
            const auto without = rk::detectMode(in);
            in.phraseRoots[9] = 1.0;
            const auto with = rk::detectMode(in);
            expectEquals(rk::noteName(without.tonic), juce::String("C"), "tie: the first candidate (as before)");
            expectEquals(rk::noteName(with.tonic), juce::String("A"), "phrase starts / ends on A");
        }

        beginTest("Scale for tuning: harmonic minor when the raised 7th is played");
        {
            const auto minor = streamSummary(keyLoop(9, true, 0.0, rate), rate); // A minor, i iv V i: the V has G#
            expectEquals(minor.tuningScale().name(), juce::String("A Harmonic Minor"));
            expect(std::find(minor.tuningScale().notes.begin(), minor.tuningScale().notes.end(), 8) != minor.tuningScale().notes.end(), "G# in the scale");
            expectEquals(minor.tuningScale().clipboardText(), juce::String("A Harmonic Minor (A B C D E F G#)"));
            const auto major = streamSummary(keyLoop(0, false, 0.0, rate), rate);
            expectEquals(major.tuningScale().name(), juce::String("C Major"));
        }

        beginTest("Saved maps from 0.1.0 (RKT1) still load");
        {
            juce::MemoryOutputStream out;
            out.writeInt(0x524b5431);
            out.writeInt(1);
            out.writeInt(0);
            rk::TimelineBin b;
            b.fine[0] = 1.0f;
            out.write(b.fine.data(), sizeof(float) * b.fine.size());
            out.write(b.bass.data(), sizeof(float) * b.bass.size());
            out.write(b.fund.data(), sizeof(float) * b.fund.size());
            for (double v : { 1.0, 0.0, 1.0, 1.0, 1.0, 0.0 })
                out.writeDouble(v);
            out.writeInt(0);
            out.writeInt(0);
            out.writeFloat(1.0f);
            rk::KeyAnalyzer a;
            a.prepare(rate);
            juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
            expect(a.readFrom(in), "RKT1 reads");
            expectEquals(static_cast<int>(a.bins().size()), 1);
            expectEquals(a.bins().begin()->second.barBeat, -1, "no bar position in old maps");
        }
    }
};

static KeyEngineTests keyEngineTests;
static TimelineTests timelineTests;
static BassTonicTests bassTonicTests;
static ModeTests modeTests;
static SpiralTests spiralTests;
static AnalysisExtrasTests analysisExtrasTests;

struct ConsoleLogger final : juce::Logger
{
    void logMessage(const juce::String& message) override { std::cout << message << std::endl; }
};

int main(int argc, char** argv)
{
    // ModeMatchTests --analyze <audio file>: the plugin's analysis of one recording (needs ffmpeg on PATH).
    // --analyze <file> <rate> <bpm>: the same at that sample rate, on a host timeline at that tempo (as in a DAW).
    if ((argc == 3 || argc == 5) && juce::String(argv[1]) == "--analyze")
    {
        const double rate = argc == 5 ? juce::String(argv[3]).getDoubleValue() : realSongRate;
        const auto audio = decodeWithFfmpeg(juce::File(juce::String::fromUTF8(argv[2])), rate);
        if (audio.empty())
        {
            std::cout << "could not decode (missing file or ffmpeg)" << std::endl;
            return 2;
        }
        printSummary(argc == 5 ? hostSummary(audio, rate, juce::String(argv[4]).getDoubleValue())
                               : streamSummary(audio, rate, 1024));
        return 0;
    }

    ConsoleLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + std::to_string(failures)) << std::endl;
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
