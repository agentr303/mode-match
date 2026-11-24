#include "PluginProcessor.h"
#include "PluginEditor.h"

ModeMatchProcessor::ModeMatchProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Mode Match analysis")
{
    monoStore.assign(monoFifoSize, 0.0f);
    recordStore.resize(recordFifoSize);
    keyAnalyzer.prepare(currentRate);
    spiral.prepare(currentRate);
}

ModeMatchProcessor::~ModeMatchProcessor()
{
    stopThread(2000);
}

bool ModeMatchProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in.isDisabled() || in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ModeMatchProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    stopThread(2000);
    monoFifo.reset();
    recordFifo.reset();
    monoBlock.assign(static_cast<std::size_t>(juce::jmax(512, samplesPerBlock)), 0.0f);
    {
        const juce::ScopedLock sl(analyzerLock);
        currentRate = sampleRate;
        keyAnalyzer.prepare(sampleRate); // keeps the timeline map: the song key survives a sample-rate change
        spiral.prepare(sampleRate);
    }
    startThread(juce::Thread::Priority::low);
}

void ModeMatchProcessor::releaseResources()
{
    stopThread(2000);
}

void ModeMatchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
    if (numSamples <= 0 || channels <= 0)
        return;

    // Where the host is (beats, seconds, playing).
    BlockRecord record;
    if (auto* host = getPlayHead())
        if (const auto info = host->getPosition())
        {
            record.position.playing = info->getIsPlaying();
            if (const auto ppq = info->getPpqPosition())
            {
                record.position.hasTimeline = true;
                record.position.ppq = *ppq;
            }
            if (const auto bpm = info->getBpm())
                record.position.bpm = *bpm > 1.0 ? *bpm : 120.0;
            if (const auto secs = info->getTimeInSeconds())
                record.position.seconds = *secs;
            else if (record.position.hasTimeline)
                record.position.seconds = record.position.ppq * 60.0 / record.position.bpm;
            if (const auto sig = info->getTimeSignature())
                record.position.beatsPerBar = juce::jmax(1, sig->numerator);
        }
    record.numSamples = numSamples;
    record.numChannels = channels;
    record.accumulate = getViewMode() == ViewMode::analyze;

    // Mono (channel average) into the FIFO; energy over all channels. An offline bounce runs faster than the analysis
    // can, so there (and only there) the audio thread waits for room rather than dropping audio.
    const auto noRoom = [&] { return monoFifo.getFreeSpace() < numSamples || recordFifo.getFreeSpace() < 1; };
    if (isNonRealtime() && isThreadRunning())
        for (int waited = 0; noRoom() && waited < 4000; ++waited)
            juce::Thread::sleep(1);
    if (static_cast<int>(monoBlock.size()) < numSamples)
        monoBlock.resize(static_cast<std::size_t>(numSamples)); // a host exceeding its announced block size: rare, allocates once
    if (noRoom())
    {
        overflowed.store(true);
        return;
    }
    const float gain = 1.0f / static_cast<float>(channels);
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float v = buffer.getReadPointer(ch)[i];
            sum += v;
            energy += static_cast<double>(v) * v;
        }
        monoBlock[static_cast<std::size_t>(i)] = sum * gain;
    }
    record.energy = energy;
    {
        const auto scope = monoFifo.write(numSamples);
        if (scope.blockSize1 > 0)
            std::copy_n(monoBlock.data(), scope.blockSize1, monoStore.data() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy_n(monoBlock.data() + scope.blockSize1, scope.blockSize2, monoStore.data() + scope.startIndex2);
    }
    {
        const auto scope = recordFifo.write(1);
        recordStore[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = record;
    }
}

// ================================================================================================================
// Analysis thread
// ================================================================================================================
void ModeMatchProcessor::run()
{
    while (! threadShouldExit())
    {
        bool didWork = false;
        bool spiralUpdated = false;

        if (clearRequested.exchange(false))
        {
            const juce::ScopedLock sl(analyzerLock);
            keyAnalyzer.clear();
            summaryFrozen = false;
            lastSummaryMs = 0;
        }

        while (recordFifo.getNumReady() > 0 && ! threadShouldExit())
        {
            BlockRecord record;
            {
                const auto scope = recordFifo.read(1);
                record = recordStore[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
            }
            if (monoFifo.getNumReady() < record.numSamples)
                break; // cannot happen: samples are written before their record
            scratch.resize(static_cast<std::size_t>(record.numSamples));
            {
                const auto scope = monoFifo.read(record.numSamples);
                if (scope.blockSize1 > 0)
                    std::copy_n(monoStore.data() + scope.startIndex1, scope.blockSize1, scratch.data());
                if (scope.blockSize2 > 0)
                    std::copy_n(monoStore.data() + scope.startIndex2, scope.blockSize2, scratch.data() + scope.blockSize1);
            }
            const juce::ScopedLock sl(analyzerLock);
            spiral.setReferenceA4(referenceA4.load());
            spiralUpdated |= spiral.push(scratch.data(), record.numSamples);
            keyAnalyzer.process(scratch.data(), record.numSamples, record.position, record.energy, record.numChannels, record.accumulate);
            lastPosition = record.position;
            didWork = true;
        }

        const auto now = juce::Time::getMillisecondCounter();
        const bool summaryDue = now - lastSummaryMs > 250 || lastSummaryMs == 0;
        if (spiralUpdated || (didWork && summaryDue))
            publish(summaryDue);
        if (summaryDue)
            lastSummaryMs = now;
        if (! didWork)
            wait(5);
    }
}

void ModeMatchProcessor::publish(bool withSummary)
{
    // Built outside the snapshot lock; the analyser lock keeps state loading out.
    Snapshot next;
    {
        const juce::ScopedLock sl(analyzerLock);
        next.partials = spiral.partials();
        next.pitch = spiral.pitch();
        next.inputLevel = spiral.inputLevel();
        next.localKey = keyAnalyzer.localKey();
        next.chord = keyAnalyzer.currentChord();
        next.beat = keyAnalyzer.currentBeat();
        next.hostTimeline = keyAnalyzer.usesHostTimeline();
        next.bpm = lastPosition.bpm;
        next.beatsPerBar = lastPosition.beatsPerBar;
        next.playing = lastPosition.playing || ! lastPosition.hasTimeline;
        if (withSummary)
        {
            const bool locked = getViewMode() == ViewMode::lock;
            if (locked && summaryFrozen)
                next.summary = frozenSummary;
            else
            {
                next.summary = keyAnalyzer.computeSummary();
                frozenSummary = next.summary;
                summaryFrozen = locked;
            }
            next.timeline.reserve(keyAnalyzer.bins().size());
            for (const auto& [index, bin] : keyAnalyzer.bins())
                next.timeline.push_back({ index, bin.localKey, bin.chord, bin.weight });
        }
    }
    const juce::SpinLock::ScopedLockType sl(snapshotLock);
    if (! withSummary)
    {
        next.summary = std::move(snapshot.summary);
        next.timeline = std::move(snapshot.timeline);
    }
    next.serial = snapshot.serial + 1;
    snapshot = std::move(next);
}

void ModeMatchProcessor::copySnapshot(Snapshot& out) const
{
    const juce::SpinLock::ScopedLockType sl(snapshotLock);
    out = snapshot;
}

// ================================================================================================================
// State: settings plus the timeline map, so each instance keeps its own song key across sessions.
// ================================================================================================================
void ModeMatchProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("ModeMatch");
    state.setProperty("version", 1, nullptr);
    state.setProperty("mode", viewMode.load(), nullptr);
    state.setProperty("a4", referenceA4.load(), nullptr);
    state.setProperty("editorWidth", editorWidth, nullptr);
    state.setProperty("editorHeight", editorHeight, nullptr);
    juce::MemoryOutputStream bins;
    {
        const juce::ScopedLock sl(analyzerLock);
        keyAnalyzer.writeTo(bins);
    }
    state.setProperty("bins", bins.getMemoryBlock(), nullptr);
    juce::MemoryOutputStream out(dest, false);
    state.writeToStream(out);
}

void ModeMatchProcessor::setStateInformation(const void* data, int size)
{
    const auto state = juce::ValueTree::readFromData(data, static_cast<std::size_t>(size));
    if (! state.isValid() || ! state.hasType("ModeMatch"))
        return;
    viewMode.store(juce::jlimit(0, 3, static_cast<int>(state.getProperty("mode", static_cast<int>(ViewMode::analyze)))));
    setReferenceA4(static_cast<float>(state.getProperty("a4", 440.0)));
    editorWidth = juce::jlimit(640, 2400, static_cast<int>(state.getProperty("editorWidth", 1200)));
    editorHeight = juce::jlimit(420, 1600, static_cast<int>(state.getProperty("editorHeight", 760)));
    if (const auto* block = state.getProperty("bins").getBinaryData())
    {
        juce::MemoryInputStream in(*block, false);
        const juce::ScopedLock sl(analyzerLock);
        keyAnalyzer.readFrom(in);
        summaryFrozen = false;
        lastSummaryMs = 0;
    }
}

juce::AudioProcessorEditor* ModeMatchProcessor::createEditor()
{
    return new ModeMatchEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModeMatchProcessor();
}
