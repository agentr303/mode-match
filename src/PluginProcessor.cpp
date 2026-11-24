#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "analysis/nuvo/KeyDetector.h"
#include "ara/ModeMatchARA.h"

ModeMatchProcessor::ModeMatchProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Mode Match analysis")
{
    monoStore.assign(monoFifoSize, 0.0f);
    recordStore.resize(recordFifoSize);
    keyAnalyzer.prepare(currentRate);
    araAnalyzer->prepare(currentRate);
    spiral.prepare(currentRate);
#if JucePlugin_Enable_ARA
    startTimerHz(2); // picks up ARA results; a no-op when no ARA host binds the plugin
#endif
}

ModeMatchProcessor::~ModeMatchProcessor()
{
    stopTimer();
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
#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#endif
    numHeldNotes = 0;
    scaleSent = 0;
    startThread(juce::Thread::Priority::low);
}

void ModeMatchProcessor::releaseResources()
{
    stopThread(2000);
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

double ModeMatchProcessor::getTailLengthSeconds() const
{
#if JucePlugin_Enable_ARA
    double tail = 0.0;
    if (getTailLengthSecondsForARA(tail))
        return tail;
#endif
    return 0.0;
}

void ModeMatchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    // ARA: the plugin plays the clip's audio itself; the spiral then follows what is rendered.
    bool araRendering = false;
#if JucePlugin_Enable_ARA
    if (isBoundToARA())
        araRendering = processBlockForARA(buffer, juce::AudioProcessor::isRealtime(), getPlayHead());
#endif

    writeMidi(midi);
    if (numSamples <= 0 || channels <= 0)
        return;

    // Where the host is (beats, seconds, playing, bar line).
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
            if (const auto bar = info->getPpqPositionOfLastBarStart())
            {
                record.position.hasBarStart = true;
                record.position.barStartPpq = *bar;
            }
        }
    record.numSamples = numSamples;
    record.numChannels = channels;
    // With ARA the clips are analysed offline, so live playback only drives the spiral and the "now" readouts.
    record.accumulate = getViewMode() == ViewMode::analyze && ! araRendering;

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
// MIDI out (PLAN.md milestone 6), on channel 1 at the start of the block, only when the scale or the setting changes:
//   key switch:  one held note on the tonic in octave 1 (C1 = 24 .. B1 = 35), and CC 20 = the scale type
//                (Settings.h scaleTypes: 0 Major, 1 Natural Minor, 2 Harmonic Minor, 3 Melodic Minor, 4 Dorian ...).
//   scale notes: the 7 scale notes held in octave 4 (C4 = 60 .. B4 = 71), for plugins that learn a scale from MIDI.
// ================================================================================================================
juce::uint32 ModeMatchProcessor::packScale(const rk::TuningScale& t)
{
    if (! t.valid())
        return 0;
    juce::uint32 mask = 0;
    for (int pc : t.notes)
        mask |= 1u << pc;
    juce::uint32 type = 0;
    for (std::size_t i = 0; i < rk::scaleTypes.size(); ++i)
        if (t.type == rk::scaleTypes[i])
            type = static_cast<juce::uint32>(i);
    return 0x80000000u | (type << 16) | (static_cast<juce::uint32>(t.tonic) << 12) | mask;
}

void ModeMatchProcessor::writeMidi(juce::MidiBuffer& midi)
{
    const int mode = midiOut.load();
    const auto scale = scaleToSend.load();
    if (mode == midiModeSent && scale == scaleSent)
        return;
    for (int i = 0; i < numHeldNotes; ++i)
        midi.addEvent(juce::MidiMessage::noteOff(1, heldNotes[static_cast<std::size_t>(i)]), 0);
    numHeldNotes = 0;
    midiModeSent = mode;
    scaleSent = scale;
    if ((scale & 0x80000000u) == 0 || mode == static_cast<int>(rk::MidiOut::off))
        return;
    const int tonic = static_cast<int>((scale >> 12) & 0xf);
    if (mode == static_cast<int>(rk::MidiOut::keySwitch))
    {
        heldNotes[0] = 24 + tonic;
        numHeldNotes = 1;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 20, static_cast<int>((scale >> 16) & 0xf)), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, heldNotes[0], static_cast<juce::uint8>(100)), 0);
    }
    else
        for (int pc = 0; pc < 12; ++pc)
            if ((scale & (1u << pc)) != 0 && numHeldNotes < static_cast<int>(heldNotes.size()))
            {
                heldNotes[static_cast<std::size_t>(numHeldNotes++)] = 60 + pc;
                midi.addEvent(juce::MidiMessage::noteOn(1, 60 + pc, static_cast<juce::uint8>(100)), 0);
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
        const bool editorOpen = openEditors.load() > 0;

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
            // The spiral engine (four FFTs every 20 ms) runs only while an editor is open; the key map keeps
            // accumulating at full rate either way.
            if (editorOpen)
            {
                spiral.setReferenceA4(referenceA4.load());
                spiralUpdated |= spiral.push(scratch.data(), record.numSamples);
            }
            keyAnalyzer.process(scratch.data(), record.numSamples, record.position, record.energy, record.numChannels, record.accumulate);
            lastPosition = record.position;
            didWork = true;
        }

        // Closed editor: the summary (for MIDI out and state) every 2 s instead of every 250 ms.
        const auto now = juce::Time::getMillisecondCounter();
        const bool summaryDue = now - lastSummaryMs > (editorOpen ? 250u : 2000u) || lastSummaryMs == 0;
        if (spiralUpdated || (didWork && summaryDue))
            publish(summaryDue);
        if (summaryDue && didWork)
            lastSummaryMs = now;
        if (! didWork)
            wait(editorOpen ? 5 : 20);
    }
}

void ModeMatchProcessor::publish(bool withSummary)
{
    // Built outside the snapshot lock; the analyser lock keeps state loading and the ARA merge out.
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
        next.ara = araActive;
        next.araSources = araSourcesReady;
        next.araSourcesTotal = araSourcesTotal;
        if (withSummary)
        {
            const auto& source = araActive ? *araAnalyzer : keyAnalyzer;
            const bool locked = getViewMode() == ViewMode::lock;
            if (locked && summaryFrozen)
                next.summary = frozenSummary;
            else
            {
                next.summary = source.computeSummary();
                frozenSummary = next.summary;
                summaryFrozen = locked;
            }
            next.timeline.reserve(source.bins().size());
            for (const auto& [index, bin] : source.bins())
                next.timeline.push_back({ index, bin.localKey, bin.chord, bin.weight });
            if (araActive)
            {
                next.hostTimeline = false;
                next.playing = false;
            }
            scaleToSend.store(packScale(next.summary.tuningScale()));
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

void ModeMatchProcessor::timerCallback()
{
#if JucePlugin_Enable_ARA
    // ARA: merge the analysed sources under this instance's regions (message thread, where the ARA model lives).
    if (! isBoundToARA())
        return;
    auto merged = std::make_unique<rk::KeyAnalyzer>();
    const auto m = rk::ara::mergeRegions(*this, *merged, currentRate, araSignature);
    if (! m.changed)
        return;
    {
        const juce::ScopedLock sl(analyzerLock);
        araAnalyzer = std::move(merged);
        araSignature = m.signature;
        araSourcesReady = m.ready;
        araSourcesTotal = m.total;
        araActive = m.ready > 0;
        summaryFrozen = false;
    }
    publish(true);
#endif
}

// ================================================================================================================
// Presets and the track name
// ================================================================================================================
void ModeMatchProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;
    currentProgram = index;
    const auto& p = rk::presets[static_cast<std::size_t>(index)];
    setReferenceA4(p.a4);
    setTopNote(p.topNote);
    setRangeIndex(p.range);
    setTransposition(p.transposition);
}

const juce::String ModeMatchProcessor::getProgramName(int index)
{
    return index >= 0 && index < getNumPrograms() ? juce::String(rk::presets[static_cast<std::size_t>(index)].name) : juce::String();
}

void ModeMatchProcessor::updateTrackProperties(const TrackProperties& properties)
{
    const auto name = properties.name.value_or(juce::String());
    {
        const juce::SpinLock::ScopedLockType sl(trackLock);
        trackName = name;
    }
    const auto label = KeyDetector::keyFromLabel(name);
    trackKeyHint.store(label.found ? rk::keyIndex(label.rootNote, label.isMinor) : -1);
}

juce::String ModeMatchProcessor::getTrackName() const
{
    const juce::SpinLock::ScopedLockType sl(trackLock);
    return trackName;
}

// ================================================================================================================
// State: settings plus the timeline map, so each instance keeps its own song key across sessions.
// ================================================================================================================
void ModeMatchProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("ModeMatch");
    state.setProperty("version", 2, nullptr);
    state.setProperty("mode", viewMode.load(), nullptr);
    state.setProperty("a4", referenceA4.load(), nullptr);
    state.setProperty("editorWidth", editorWidth, nullptr);
    state.setProperty("editorHeight", editorHeight, nullptr);
    state.setProperty("topNote", topNote.load(), nullptr);
    state.setProperty("range", rangeIndex.load(), nullptr);
    state.setProperty("transposition", transposition.load(), nullptr);
    state.setProperty("tuneTarget", tuneTarget.load(), nullptr);
    state.setProperty("midiOut", midiOut.load(), nullptr);
    state.setProperty("gpu", gpuEnabled.load(), nullptr);
    state.setProperty("program", currentProgram, nullptr);
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
    setTopNote(state.getProperty("topNote", 0));
    setRangeIndex(state.getProperty("range", 0));
    setTransposition(state.getProperty("transposition", 0));
    setTuneTarget(state.getProperty("tuneTarget", -1));
    midiOut.store(juce::jlimit(0, 2, static_cast<int>(state.getProperty("midiOut", 0))));
    gpuEnabled.store(state.getProperty("gpu", true));
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(state.getProperty("program", 0)));
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
