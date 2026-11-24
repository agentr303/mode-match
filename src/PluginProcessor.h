#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/KeyAnalyzer.h"
#include "analysis/SpiralAnalyzer.h"

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
    juce::uint32 serial = 0;
};

class ModeMatchProcessor final : public juce::AudioProcessor, private juce::Thread
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
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Editor side.
    void copySnapshot(Snapshot& out) const;
    [[nodiscard]] ViewMode getViewMode() const { return static_cast<ViewMode>(viewMode.load()); }
    void setViewMode(ViewMode m) { viewMode.store(static_cast<int>(m)); }
    [[nodiscard]] float getReferenceA4() const { return referenceA4.load(); }
    void setReferenceA4(float hz) { referenceA4.store(juce::jlimit(400.0f, 480.0f, hz)); }
    void requestClear() { clearRequested.store(true); }
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

    void run() override; // the analysis thread
    void publish(bool withSummary);

    static constexpr int monoFifoSize = 1 << 18;
    static constexpr int recordFifoSize = 4096;
    juce::AbstractFifo monoFifo { monoFifoSize }, recordFifo { recordFifoSize };
    std::vector<float> monoStore, scratch, monoBlock;
    std::vector<BlockRecord> recordStore;
    std::atomic<bool> overflowed { false };

    std::atomic<int> viewMode { static_cast<int>(ViewMode::analyze) };
    std::atomic<float> referenceA4 { 440.0f };
    std::atomic<bool> clearRequested { false };

    mutable juce::CriticalSection analyzerLock; // the analysis thread vs state save / load
    rk::KeyAnalyzer keyAnalyzer;
    rk::SpiralAnalyzer spiral;
    double currentRate = 48000.0;
    rk::HostPosition lastPosition;

    mutable juce::SpinLock snapshotLock;
    Snapshot snapshot;
    rk::KeySummary frozenSummary;
    bool summaryFrozen = false;
    std::atomic<juce::uint32> lastSummaryMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeMatchProcessor)
};
