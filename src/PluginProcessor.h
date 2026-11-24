#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Settings.h"
#include "analysis/KeyAnalyzer.h"
#include "analysis/SpiralAnalyzer.h"

#ifndef JucePlugin_Enable_ARA
 #define JucePlugin_Enable_ARA 0
#endif

// What the editor draws, published by the analysis thread (PLAN.md 3: audio thread -> FIFO -> analysis thread ->
// snapshot -> UI timer). The audio thread never takes a lock and never allocates.
enum class ViewMode { listen = 0, analyze, lock, tune };

struct TimelineCell
{
    int index = 0;      // beat (host timeline) or half second (free-running)
    int localKey = -1;
    int chord = -1;
    float weight = 0.0f;
};

struct Snapshot
{
    std::vector<rk::Partial> partials;
    rk::PitchReading pitch;
    float inputLevel = 0.0f;
    int localKey = -1, chord = -1;
    rk::KeySummary summary;
    std::vector<TimelineCell> timeline;
    double beat = 0.0, bpm = 120.0;
    int beatsPerBar = 4;
    bool hostTimeline = false, playing = false;
    // ARA: the summary comes from the clips' audio, read offline (araSources analysed of araSourcesTotal).
    bool ara = false;
    int araSources = 0, araSourcesTotal = 0;
    juce::uint32 serial = 0;
};

class ModeMatchProcessor final : public juce::AudioProcessor,
#if JucePlugin_Enable_ARA
                                 public juce::AudioProcessorARAExtension,
#endif
                                 private juce::Thread,
                                 private juce::Timer
{
public:
    ModeMatchProcessor();
    ~ModeMatchProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Factory presets (Settings.h): reference pitch, spiral layout and transposition. The analysis is never touched.
    int getNumPrograms() override { return static_cast<int>(rk::presets.size()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // The host's track name (VST3 / AU / CLAP): a Camelot label in it ("8A 124 Bassline") is shown as a cross-check.
    void updateTrackProperties(const TrackProperties&) override;

    // Editor side.
    void copySnapshot(Snapshot& out) const;
    [[nodiscard]] ViewMode getViewMode() const { return static_cast<ViewMode>(viewMode.load()); }
    void setViewMode(ViewMode m) { viewMode.store(static_cast<int>(m)); }
    [[nodiscard]] float getReferenceA4() const { return referenceA4.load(); }
    void setReferenceA4(float hz) { referenceA4.store(juce::jlimit(400.0f, 480.0f, hz)); }
    void requestClear() { clearRequested.store(true); }

    [[nodiscard]] int getTopNote() const { return topNote.load(); }
    void setTopNote(int pc) { topNote.store(((pc % 12) + 12) % 12); }
    [[nodiscard]] int getRangeIndex() const { return rangeIndex.load(); }
    void setRangeIndex(int i) { rangeIndex.store(juce::jlimit(0, static_cast<int>(rk::spiralRanges.size()) - 1, i)); }
    [[nodiscard]] int getTransposition() const { return transposition.load(); }
    void setTransposition(int i) { transposition.store(juce::jlimit(0, static_cast<int>(rk::transpositions.size()) - 1, i)); }
    [[nodiscard]] int getTuneTarget() const { return tuneTarget.load(); }
    void setTuneTarget(int pc) { tuneTarget.store(pc < 0 ? -1 : pc % 12); }
    [[nodiscard]] rk::MidiOut getMidiOut() const { return static_cast<rk::MidiOut>(midiOut.load()); }
    void setMidiOut(rk::MidiOut m) { midiOut.store(static_cast<int>(m)); }
    [[nodiscard]] bool getGpuEnabled() const { return gpuEnabled.load(); }
    void setGpuEnabled(bool on) { gpuEnabled.store(on); }
    [[nodiscard]] juce::String getTrackName() const;
    [[nodiscard]] int getTrackKeyHint() const { return trackKeyHint.load(); }

    // Open editors: with none, the spiral engine sleeps and only the key map keeps accumulating (PLAN.md 7).
    void editorOpened() { ++openEditors; }
    void editorClosed() { --openEditors; }

    int editorWidth = 1200, editorHeight = 760;

private:
    struct BlockRecord
    {
        rk::HostPosition position;
        int numSamples = 0;
        int numChannels = 1;
        double energy = 0.0;
        bool accumulate = false;
    };

    void run() override;        // the analysis thread
    void timerCallback() override; // message thread: ARA results
    void publish(bool withSummary);
    void writeMidi(juce::MidiBuffer&);
    static juce::uint32 packScale(const rk::TuningScale&);

    static constexpr int monoFifoSize = 1 << 18;
    static constexpr int recordFifoSize = 4096;
    juce::AbstractFifo monoFifo { monoFifoSize }, recordFifo { recordFifoSize };
    std::vector<float> monoStore, scratch, monoBlock;
    std::vector<BlockRecord> recordStore;
    std::atomic<bool> overflowed { false };

    std::atomic<int> viewMode { static_cast<int>(ViewMode::analyze) };
    std::atomic<float> referenceA4 { 440.0f };
    std::atomic<bool> clearRequested { false };
    std::atomic<int> topNote { 0 }, rangeIndex { 0 }, transposition { 0 }, tuneTarget { -1 };
    std::atomic<int> midiOut { static_cast<int>(rk::MidiOut::off) };
    std::atomic<bool> gpuEnabled { true };
    std::atomic<int> openEditors { 0 };
    int currentProgram = 0;

    mutable juce::SpinLock trackLock;
    juce::String trackName;
    std::atomic<int> trackKeyHint { -1 };

    // MIDI out: the scale to send (packed, written by the analysis thread) and what the audio thread holds.
    std::atomic<juce::uint32> scaleToSend { 0 };
    juce::uint32 scaleSent = 0;
    int midiModeSent = 0;
    std::array<int, 8> heldNotes {};
    int numHeldNotes = 0;

    mutable juce::CriticalSection analyzerLock; // the analysis thread vs state save / load / ARA merge
    rk::KeyAnalyzer keyAnalyzer;
    rk::SpiralAnalyzer spiral;
    double currentRate = 48000.0;
    rk::HostPosition lastPosition;

    // ARA: the clips' map, merged from each audio source's offline analysis (message thread, under analyzerLock).
    std::unique_ptr<rk::KeyAnalyzer> araAnalyzer = std::make_unique<rk::KeyAnalyzer>();
    bool araActive = false;
    int araSourcesReady = 0, araSourcesTotal = 0;
    juce::String araSignature;

    mutable juce::SpinLock snapshotLock;
    Snapshot snapshot;
    rk::KeySummary frozenSummary;
    bool summaryFrozen = false;
    std::atomic<juce::uint32> lastSummaryMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeMatchProcessor)
};
