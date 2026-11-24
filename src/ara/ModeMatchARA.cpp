#include "ModeMatchARA.h"

#if JucePlugin_Enable_ARA

namespace rk::ara
{
// =============================================================================================================
// The offline analysis of one audio source.
// =============================================================================================================
class AnalysedAudioSource::Job final : public juce::ThreadPoolJob
{
public:
    Job(AnalysedAudioSource& ownerIn, std::unique_ptr<juce::ARAAudioSourceReader> readerIn)
        : juce::ThreadPoolJob("Mode Match ARA analysis"), owner(ownerIn), reader(std::move(readerIn))
    {
    }

    JobStatus runJob() override
    {
        const auto length = reader->lengthInSamples;
        const double rate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
        const int channels = juce::jmax(1, static_cast<int>(reader->numChannels));
        auto analyzer = std::make_shared<KeyAnalyzer>();
        analyzer->prepare(rate);

        constexpr int chunk = 32768;
        juce::AudioBuffer<float> buffer(channels, chunk);
        std::vector<float> mono(static_cast<std::size_t>(chunk));
        for (juce::int64 pos = 0; pos < length; pos += chunk)
        {
            if (shouldExit())
                return jobHasFinished;
            const int n = static_cast<int>(juce::jmin<juce::int64>(chunk, length - pos));
            if (! reader->read(&buffer, 0, n, pos, true, true))
                return jobHasFinished; // samples access withdrawn: no result
            // Source time on a fixed 120 bpm grid: one "beat" per 0.5 s, so bin = half second of the source.
            HostPosition position;
            position.hasTimeline = position.playing = true;
            position.bpm = 60.0 / binSeconds;
            position.seconds = static_cast<double>(pos) / rate;
            position.ppq = position.seconds / binSeconds;
            double energy = 0.0;
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float v = buffer.getReadPointer(ch)[i];
                    sum += v;
                    energy += static_cast<double>(v) * v;
                }
                mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(channels);
            }
            analyzer->process(mono.data(), n, position, energy, channels, true);
            owner.progressValue.store(static_cast<float>(static_cast<double>(pos + n) / static_cast<double>(juce::jmax<juce::int64>(1, length))));
        }
        analyzer->finish();
        owner.finished(std::move(analyzer));
        return jobHasFinished;
    }

private:
    AnalysedAudioSource& owner;
    std::unique_ptr<juce::ARAAudioSourceReader> reader;
};

AnalysedAudioSource::AnalysedAudioSource(juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef)
    : juce::ARAAudioSource(document, hostRef)
{
    addListener(this);
}

AnalysedAudioSource::~AnalysedAudioSource()
{
    stopAnalysis();
    removeListener(this);
}

std::shared_ptr<const KeyAnalyzer> AnalysedAudioSource::result() const
{
    const std::lock_guard<std::mutex> lock(resultLock);
    return analysis;
}

void AnalysedAudioSource::finished(std::shared_ptr<const KeyAnalyzer> done)
{
    const std::lock_guard<std::mutex> lock(resultLock);
    analysis = std::move(done);
}

void AnalysedAudioSource::startAnalysis()
{
    stopAnalysis();
    if (! isSampleAccessEnabled())
        return;
    progressValue.store(0.0f);
    job = std::make_unique<Job>(*this, std::make_unique<juce::ARAAudioSourceReader>(this));
    pool->addJob(job.get(), false);
}

void AnalysedAudioSource::stopAnalysis()
{
    if (job != nullptr)
    {
        pool->removeJob(job.get(), true, 10000);
        job.reset();
    }
}

void AnalysedAudioSource::willEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable)
{
    if (! enable)
        stopAnalysis(); // the reader must not run once access is withdrawn
}

void AnalysedAudioSource::didEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable)
{
    if (enable && result() == nullptr)
        startAnalysis();
}

void AnalysedAudioSource::doUpdateAudioSourceContent(juce::ARAAudioSource*, juce::ARAContentUpdateScopes scope)
{
    if (scope.affectSamples())
    {
        finished(nullptr);
        startAnalysis();
    }
}

void AnalysedAudioSource::willDestroyAudioSource(juce::ARAAudioSource*)
{
    stopAnalysis();
}

// =============================================================================================================
// Playback: the regions' audio, unchanged (after JUCE's ARAPluginDemo renderer).
// =============================================================================================================
void PlaybackRenderer::prepareToPlay(double rate, int maxBlock, int channels, juce::AudioProcessor::ProcessingPrecision,
                                     AlwaysNonRealtime alwaysNonRealtime)
{
    sampleRate = rate;
    numChannels = channels;
    maximumSamplesPerBlock = maxBlock;
    tempBuffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, maximumSamplesPerBlock);
    buffered = alwaysNonRealtime == AlwaysNonRealtime::no;
    readers.clear();
    for (auto* region : getPlaybackRegions())
    {
        auto* source = region->getAudioModification()->getAudioSource();
        if (readers.count(source) != 0)
            continue;
        auto reader = std::make_unique<juce::ARAAudioSourceReader>(source);
        Reader r;
        if (buffered)
        {
            auto b = std::make_unique<juce::BufferingAudioReader>(reader.release(), *readAheadThread,
                                                                  juce::jmax(4 * maximumSamplesPerBlock, juce::roundToInt(2.0 * sampleRate)));
            r.buffered = b.get();
            r.reader = std::move(b);
        }
        else
            r.reader = std::move(reader);
        readers.emplace(source, std::move(r));
    }
}

void PlaybackRenderer::releaseResources()
{
    readers.clear();
    tempBuffer.reset();
}

bool PlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                                    const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const int numSamples = buffer.getNumSamples();
    bool success = true, rendered = false;
    if (positionInfo.getIsPlaying() && tempBuffer != nullptr && numSamples <= maximumSamplesPerBlock)
    {
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength(positionInfo.getTimeInSamples().orFallback(0), numSamples);
        for (auto* region : getPlaybackRegions())
        {
            const auto playRange = region->getSampleRange(sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith(playRange);
            if (renderRange.isEmpty())
                continue;
            const juce::Range<juce::int64> modRange { region->getStartInAudioModificationSamples(), region->getEndInAudioModificationSamples() };
            const auto offset = modRange.getStart() - playRange.getStart();
            renderRange = renderRange.getIntersectionWith(modRange.movedToStartAt(playRange.getStart()));
            if (renderRange.isEmpty())
                continue;

            auto* source = region->getAudioModification()->getAudioSource();
            const auto it = readers.find(source);
            if (it == readers.end() || ! juce::approximatelyEqual(source->getSampleRate(), sampleRate))
            {
                success = false; // a sample-rate mismatch is left to the host
                continue;
            }
            if (it->second.buffered != nullptr)
                it->second.buffered->setReadTimeout(realtime == juce::AudioProcessor::Realtime::no ? 100 : 0);

            const int count = static_cast<int>(renderRange.getLength());
            const int startInBuffer = static_cast<int>(renderRange.getStart() - blockRange.getStart());
            auto& target = rendered ? *tempBuffer : buffer;
            if (! it->second.reader->read(&target, startInBuffer, count, renderRange.getStart() + offset, true, true))
            {
                success = false;
                continue;
            }
            if (rendered)
                for (int c = 0; c < juce::jmin(numChannels, buffer.getNumChannels()); ++c)
                    buffer.addFrom(c, startInBuffer, *tempBuffer, c, startInBuffer, count);
            else
            {
                if (startInBuffer > 0)
                    buffer.clear(0, startInBuffer);
                if (startInBuffer + count < numSamples)
                    buffer.clear(startInBuffer + count, numSamples - startInBuffer - count);
                rendered = true;
            }
        }
    }
    if (! rendered)
        buffer.clear();
    return success;
}

// =============================================================================================================
juce::ARAAudioSource* DocumentController::doCreateAudioSource(juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef) noexcept
{
    return new AnalysedAudioSource(document, hostRef);
}

juce::ARAPlaybackRenderer* DocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer(getDocumentController());
}

MergeResult mergeRegions(juce::AudioProcessorARAExtension& extension, KeyAnalyzer& out, double sampleRate, const juce::String& previous)
{
    std::vector<juce::ARAPlaybackRegion*> regions;
    if (auto* renderer = extension.getPlaybackRenderer())
        for (auto* r : renderer->getPlaybackRegions())
            regions.push_back(r);
    if (auto* editor = extension.getEditorRenderer())
        for (auto* r : editor->getPlaybackRegions())
            if (std::find(regions.begin(), regions.end(), r) == regions.end())
                regions.push_back(r);
    std::sort(regions.begin(), regions.end(),
              [](auto* a, auto* b) { return a->getStartInPlaybackTime() < b->getStartInPlaybackTime(); });

    MergeResult m;
    std::vector<std::pair<juce::ARAPlaybackRegion*, std::shared_ptr<const KeyAnalyzer>>> parts;
    for (auto* region : regions)
    {
        auto* source = dynamic_cast<AnalysedAudioSource*>(region->getAudioModification()->getAudioSource());
        if (source == nullptr)
            continue;
        ++m.total;
        auto result = source->result();
        m.signature << juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(region)) << ":"
                    << region->getStartInAudioModificationTime() << "-" << region->getEndInAudioModificationTime() << ":"
                    << juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(result.get())) << ";";
        if (result != nullptr)
        {
            ++m.ready;
            parts.emplace_back(region, std::move(result));
        }
    }

    m.changed = m.signature != previous;
    if (! m.changed)
        return m;
    out.prepare(parts.empty() ? sampleRate : parts.front().second->getSampleRate());
    out.clear();
    int indexOffset = 0;
    for (const auto& [region, result] : parts)
    {
        const int first = static_cast<int>(std::floor(region->getStartInAudioModificationTime() / binSeconds));
        const int last = static_cast<int>(std::ceil(region->getEndInAudioModificationTime() / binSeconds));
        out.importBins(*result, indexOffset - first, (indexOffset - first) * binSeconds, first, last);
        indexOffset += last - first + 1;
    }
    return m;
}
} // namespace rk::ara

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<rk::ara::DocumentController>();
}

#endif
