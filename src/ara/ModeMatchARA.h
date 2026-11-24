#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "analysis/KeyAnalyzer.h"

#if JucePlugin_Enable_ARA

// ARA 2 (PLAN.md 3): in Studio One, Cubase, Logic, Reaper and Cakewalk the plugin can read a clip's whole audio
// without it being played. Each audio source is analysed once, offline, on a background thread, with the same
// KeyAnalyzer as live playback (one bin per 0.5 s of source time). An instance's song key is then the merge of the
// sources under its playback regions, each cut to the part the region plays.
namespace rk::ara
{
// Seconds of source audio per bin.
inline constexpr double binSeconds = 0.5;

class AnalysedAudioSource final : public juce::ARAAudioSource, private juce::ARAAudioSource::Listener
{
public:
    AnalysedAudioSource(juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef);
    ~AnalysedAudioSource() override;

    // The finished analysis (null while running or before samples access).
    [[nodiscard]] std::shared_ptr<const KeyAnalyzer> result() const;
    [[nodiscard]] float progress() const { return progressValue.load(); }

private:
    class Job;

    void willEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void didEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void doUpdateAudioSourceContent(juce::ARAAudioSource*, juce::ARAContentUpdateScopes) override;
    void willDestroyAudioSource(juce::ARAAudioSource*) override;

    void startAnalysis();
    void stopAnalysis();
    void finished(std::shared_ptr<const KeyAnalyzer>);

    juce::SharedResourcePointer<juce::ThreadPool> pool;
    std::unique_ptr<Job> job;
    mutable std::mutex resultLock;
    std::shared_ptr<const KeyAnalyzer> analysis;
    std::atomic<float> progressValue { 0.0f };
};

// One read-ahead thread shared by every renderer.
class ReadAheadThread final : public juce::TimeSliceThread
{
public:
    ReadAheadThread() : juce::TimeSliceThread("Mode Match ARA read-ahead") { startThread(juce::Thread::Priority::high); }
    ~ReadAheadThread() override { stopThread(2000); }
};

// Plays the regions' audio (an ARA plugin replaces the clip's playback) - the plugin itself changes nothing.
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    using juce::ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay(double sampleRate, int maximumSamplesPerBlock, int numChannels, juce::AudioProcessor::ProcessingPrecision,
                       AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;
    bool processBlock(juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                      const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;
    using juce::ARAPlaybackRenderer::processBlock;

private:
    struct Reader
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::BufferingAudioReader* buffered = nullptr; // set when `reader` is a BufferingAudioReader
    };
    juce::SharedResourcePointer<ReadAheadThread> readAheadThread;
    std::map<juce::ARAAudioSource*, Reader> readers;
    std::unique_ptr<juce::AudioBuffer<float>> tempBuffer;
    double sampleRate = 48000.0;
    int numChannels = 2, maximumSamplesPerBlock = 512;
    bool buffered = true;
};

class DocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

protected:
    juce::ARAAudioSource* doCreateAudioSource(juce::ARADocument*, ARA::ARAAudioSourceHostRef) noexcept override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    // Nothing to archive: the analysis is recomputed from the audio when a project loads.
    bool doRestoreObjectsFromStream(juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override { return true; }
    bool doStoreObjectsToStream(juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override { return true; }
};

// The playback regions this instance plays or shows (renderer and editor roles), with their sources' results merged
// into `out` in playback order. Returns how many sources are analysed and how many there are; `signature` changes
// whenever the regions or any result changes, so callers can skip work. Message thread.
struct MergeResult
{
    int ready = 0, total = 0;
    juce::String signature;
    bool changed = false; // false: same as `previous`, `out` left alone
};
MergeResult mergeRegions(juce::AudioProcessorARAExtension& extension, KeyAnalyzer& out, double sampleRate, const juce::String& previous);
} // namespace rk::ara

#endif
