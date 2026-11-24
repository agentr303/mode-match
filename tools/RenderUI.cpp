// Renders the editor to PNG after feeding the real processor synthesised music through a fake host timeline.
//   ModeMatchRender <out-dir>
// Writes analyze.png (a song in D Dorian with a section in E minor), tune.png (a +8 cent A3) and compact.png.
//   ModeMatchRender <out-dir> <audio file> [rate] [bpm]
// Plays a recording (decoded in stereo by ffmpeg on PATH) through the processor on a host timeline, prints the summary
// and writes song.png.
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
struct FakeHost final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition(ppq);
        info.setBpm(bpm);
        info.setIsPlaying(playing);
        info.setTimeInSeconds(ppq * 60.0 / bpm);
        info.setTimeSignature(TimeSignature { 4, 4 });
        return info;
    }
};

void addTone(std::vector<float>& out, double rate, double hz, float level, int from, int count)
{
    for (int h = 1; h <= 4; ++h)
    {
        const double f = hz * h;
        if (f > rate * 0.45)
            break;
        for (int i = 0; i < count && from + i < static_cast<int>(out.size()); ++i)
        {
            const float env = static_cast<float>(juce::jmin(1.0, juce::jmin(i, count - 1 - i) / (0.01 * rate)));
            out[static_cast<std::size_t>(from + i)] += level / static_cast<float>(h * h) * env
                                                       * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * f * i / rate));
        }
    }
}

double hz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

// Two-bar chords at 120 bpm: [bass, chord tones...].
std::vector<float> song(double rate)
{
    struct Chord { int bass; std::vector<int> notes; };
    // D Dorian vamp (Dm7 - G - Dm7 - C), 8 times; then E minor (Em - C - D - Em) 2 times; then the vamp 4 times.
    const std::vector<Chord> vamp { { 38, { 62, 65, 69, 72 } }, { 43, { 62, 67, 71 } }, { 38, { 62, 65, 69, 72 } }, { 36, { 60, 64, 67 } } };
    const std::vector<Chord> eMinor { { 40, { 64, 67, 71 } }, { 36, { 60, 64, 67 } }, { 38, { 62, 66, 69 } }, { 40, { 64, 67, 71 } } };
    std::vector<Chord> plan;
    for (int i = 0; i < 8; ++i) plan.insert(plan.end(), vamp.begin(), vamp.end());
    for (int i = 0; i < 2; ++i) plan.insert(plan.end(), eMinor.begin(), eMinor.end());
    for (int i = 0; i < 4; ++i) plan.insert(plan.end(), vamp.begin(), vamp.end());
    const int chordLength = static_cast<int>(rate * 4.0); // two bars at 120 bpm
    std::vector<float> out(static_cast<std::size_t>(chordLength) * plan.size(), 0.0f);
    for (std::size_t k = 0; k < plan.size(); ++k)
    {
        const int from = static_cast<int>(k) * chordLength;
        addTone(out, rate, hz(plan[k].bass), 0.30f, from, chordLength);
        for (int n : plan[k].notes)
            addTone(out, rate, hz(n), 0.08f, from, chordLength);
    }
    return out;
}

void feed(ModeMatchProcessor& p, FakeHost& host, const std::vector<float>& audio, double rate, int block)
{
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i + block <= static_cast<int>(audio.size()); i += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, audio.data() + i, block);
        host.ppq = i / rate * host.bpm / 60.0;
        p.processBlock(buffer, midi);
        juce::Thread::sleep(1); // ~10x real time: the analysis thread keeps up (a faster feed overflows the FIFO)
    }
}

// Stereo float decode by ffmpeg: interleaved L R.
std::vector<float> decodeStereo(const juce::File& file, double rate)
{
    const auto raw = juce::File::createTempFile(".f32");
    juce::ChildProcess ffmpeg;
    if (! ffmpeg.start(juce::StringArray { "ffmpeg", "-v", "error", "-y", "-i", file.getFullPathName(), "-ac", "2", "-ar",
                                           juce::String(juce::roundToInt(rate)), "-f", "f32le", raw.getFullPathName() }, 0)
        || ! ffmpeg.waitForProcessToFinish(300000) || ffmpeg.getExitCode() != 0)
        return {};
    juce::MemoryBlock data;
    raw.loadFileAsData(data);
    raw.deleteFile();
    std::vector<float> out(data.getSize() / sizeof(float));
    std::memcpy(out.data(), data.getData(), out.size() * sizeof(float));
    return out;
}

void save(ModeMatchEditor& editor, const juce::File& file)
{
    editor.refresh();
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
    std::cout << "wrote " << file.getFullPathName() << std::endl;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    const int block = 512;
    if (argc >= 3)
    {
        const double songRate = argc > 3 ? juce::String(argv[3]).getDoubleValue() : 44100.0;
        const auto stereo = decodeStereo(juce::File(juce::String::fromUTF8(argv[2])), songRate);
        if (stereo.empty())
        {
            std::cout << "could not decode" << std::endl;
            return 2;
        }
        ModeMatchProcessor processor;
        FakeHost host;
        host.bpm = argc > 4 ? juce::String(argv[4]).getDoubleValue() : 122.0;
        processor.setPlayHead(&host);
        processor.setPlayConfigDetails(2, 2, songRate, block);
        processor.prepareToPlay(songRate, block);
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        const int frames = static_cast<int>(stereo.size() / 2);
        for (int i = 0; i + block <= frames; i += block)
        {
            for (int k = 0; k < block; ++k)
            {
                buffer.setSample(0, k, stereo[static_cast<std::size_t>(2 * (i + k))]);
                buffer.setSample(1, k, stereo[static_cast<std::size_t>(2 * (i + k) + 1)]);
            }
            host.ppq = i / songRate * host.bpm / 60.0;
            processor.processBlock(buffer, midi);
            if ((i / block) % 4 == 0)
                juce::Thread::sleep(1);
        }
        juce::Thread::sleep(2000);
        Snapshot snap;
        processor.copySnapshot(snap);
        const auto& s = snap.summary;
        std::cout << "shown " << s.displayName() << " | profile " << s.key << " | majorMinorKey " << s.majorMinorKey << " | stage "
                  << s.mode.name() << " margin " << s.mode.margin << " | bins " << s.analysedBins << std::endl;
        std::unique_ptr<ModeMatchEditor> editor(static_cast<ModeMatchEditor*>(processor.createEditor()));
        editor->setSize(1200, 760);
        save(*editor, dir.getChildFile("song.png"));
        processor.releaseResources();
        return 0;
    }
    const double rate = 48000.0;

    ModeMatchProcessor processor;
    FakeHost host;
    processor.setPlayHead(&host);
    processor.setPlayConfigDetails(2, 2, rate, block);
    processor.prepareToPlay(rate, block);

    const auto audio = song(rate);
    // The song, stopping 6 bars before the end so the playhead sits inside it.
    std::vector<float> part(audio.begin(), audio.end() - static_cast<long>(rate * 12.0));
    feed(processor, host, part, rate, block);
    juce::Thread::sleep(1500);
    {
        Snapshot snap;
        processor.copySnapshot(snap);
        const auto& m = snap.summary.mode;
        std::cout << "key " << snap.summary.displayName() << " | stage " << m.name() << " modal " << m.modal << " hint " << m.hint
                  << " margin " << m.margin << " char " << m.characteristicShare << " repl " << m.replacedShare << std::endl;
    }

    {
        std::unique_ptr<ModeMatchEditor> editor(static_cast<ModeMatchEditor*>(processor.createEditor()));
        editor->setSize(1200, 760);
        save(*editor, dir.getChildFile("analyze.png"));
        editor->setSize(720, 460);
        save(*editor, dir.getChildFile("compact.png"));
    }

    // Tune: a single A3, 8 cents sharp, transport stopped.
    processor.setViewMode(ViewMode::tune);
    host.playing = false;
    std::vector<float> note(static_cast<std::size_t>(rate * 2.0), 0.0f);
    addTone(note, rate, hz(57.08), 0.4f, 0, static_cast<int>(note.size()));
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i + block <= static_cast<int>(note.size()); i += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, note.data() + i, block);
        processor.processBlock(buffer, midi);
    }
    juce::Thread::sleep(800);
    {
        std::unique_ptr<ModeMatchEditor> editor(static_cast<ModeMatchEditor*>(processor.createEditor()));
        editor->setSize(1200, 760);
        for (int i = 0; i < 12; ++i)
            editor->refresh(); // let the tuner's smoothing settle
        save(*editor, dir.getChildFile("tune.png"));
    }
    processor.releaseResources();
    return 0;
}
