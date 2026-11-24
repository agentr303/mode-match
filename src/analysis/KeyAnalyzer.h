#pragma once

#include <climits>
#include <complex>
#include <deque>
#include <map>
#include <vector>

#include "KeyTypes.h"
#include "ModeDetector.h"
#include "signalsmith-linear/fft.h"

namespace rk
{
// ============================================================================================================
// NuvoDeck KeyDetector, streaming (PLAN.md 4a).
//
// KeyFrameAnalyzer is KeyDetector::computeChroma's per-frame code, line for line: 16384-point Hann frame,
// 100..5000 Hz, peaks that are the highest within +-3 bins and within 50 dB of the frame's loudest, placed between
// bins by a parabola through the log magnitudes, weighted log1p(power / floor).
//
// KeyAnalyzer feeds frames as audio arrives and files their peaks in a timeline map: one bin per beat of the host
// timeline (or per half second when there is no timeline). A bin keeps a 10-cent fine chroma, so pitch classes can be
// re-folded once the tuning is known, exactly like NuvoDeck's two passes, without keeping every peak. Playing a section
// again replaces its bins instead of adding to them.
//
// The summary is NuvoDeck's aggregation over that map: 30 s blocks of host time, each normalised to its max and
// weighted (blockRms / maxRms)^2, then Pearson against Temperley's profiles. One change: the trailing partial block
// counts too (weighted by its length), so loops shorter than 30 s and song endings are not dropped.
// ============================================================================================================

struct Peak
{
    double midi;
    double weight;
};

class KeyFrameAnalyzer
{
public:
    static constexpr int frameSize = 16384;
    static constexpr int hopSize = 4096;

    KeyFrameAnalyzer();
    // frame: frameSize mono samples (the channel average, not yet windowed). Appends the frame's peaks to `out`.
    // Returns false for silence. `lowPeaks` (optional, Mode Match's addition) receives the same kind of peaks between
    // 35 and 100 Hz, below KeyDetector's band: bass fundamentals for the bass chroma and the overtone check. They never
    // enter `out`, so KeyDetector's chroma is unchanged.
    bool analyse(const float* frame, double sampleRate, std::vector<Peak>& out, std::vector<Peak>* lowPeaks = nullptr);

    // Mode Match's addition, read after analyse(): the frame's log magnitudes (0.5 * ln power) for bins
    // firstValidBin()..lastValidBin(), for the percussion mask on the chord path.
    [[nodiscard]] const std::vector<double>& logMagnitudes() const { return logMagnitude; }
    [[nodiscard]] int firstValidBin() const { return validFirst; }
    [[nodiscard]] int lastValidBin() const { return validLast; }

private:
    signalsmith::linear::Pow2RealFFT<float> fft { frameSize };
    std::vector<std::complex<float>> freq;
    std::vector<float> windowed, window;
    std::vector<double> logMagnitude;
    int validFirst = 0, validLast = -1;
};

struct HostPosition
{
    bool hasTimeline = false; // the host gives a beat position
    bool playing = false;
    double ppq = 0.0;         // beats from the song start
    double bpm = 120.0;
    double seconds = 0.0;     // host time from the song start
    int beatsPerBar = 4;
    bool hasBarStart = false; // the host gives the last bar line
    double barStartPpq = 0.0; // beat position of the bar the block starts in
};

struct TimelineBin
{
    static constexpr int fineBins = 120; // 10 cents per bin, across one octave
    std::array<float, fineBins> fine {}; // peak weights by (untuned) pitch, all peaks
    std::array<float, fineBins> bass {}; // peaks below 250 Hz
    std::array<float, fineBins> fund {}; // peaks that are not the 3rd/5th/7th harmonic of a stronger lower peak (mode stage)
    double re = 0.0, im = 0.0;           // tuning sums: sum of w * e^(i 2 pi frac)
    double energy = 0.0;                 // sum of squares over all channels (for the block loudness)
    double energyCount = 0.0;            // samples x channels behind `energy`
    double samples = 0.0;                // audio samples filed here
    double seconds = 0.0;                // host time at this bin
    int pass = -1;                       // which pass over the timeline wrote it
    int localKey = -1;                   // key index heard here (smoothed)
    int chord = -1;                      // chord: root + 12 * quality (ChordQuality)
    float weight = 0.0f;                 // total peak weight
    int bar = -1, barBeat = -1;          // bar number and beat within it (host timeline only, else -1)
    int beatsPerBar = 0;
};

struct KeySummary
{
    bool valid = false;
    int key = -1, runnerUp = -1;     // NuvoDeck's major/minor answer and the runner-up (key indices)
    double r = 0.0, r2 = 0.0;        // their Pearson correlations
    Chroma presence {};              // share of each pitch class, sums to 1
    Chroma aggregate {};             // NuvoDeck's aggregate chroma (max = 1)
    Chroma bass {}, chordRoots {};   // the mode stage's tonic evidence (unnormalised)
    Chroma phraseRoots {};           // chord roots on downbeats and phrase starts / ends (host timeline only)
    double tuningCents = 0.0;        // offset from A440 equal temperament
    ModeResult mode;                 // the mode stage
    int analysedBins = 0;
    int keyChanges = 0;

    // The major/minor key shown when no other mode is named: NuvoDeck's answer, unless the mode stage found a plain
    // major/minor tonic by a clear margin from the bass and chord roots. Profile correlation alone reads tonic and
    // relative/dominant as near-ties when the chroma is flat or the root sits below KeyDetector's 100 Hz band (a dance
    // track in F minor with an F bass came out Ab major / C minor). Set by computeSummary.
    int majorMinorKey = -1;

    // What the display leads with: the mode when one is named, else the major/minor key above.
    [[nodiscard]] juce::String displayName() const { return mode.modal ? mode.name() : keyLongName(majorMinorKey); }
    [[nodiscard]] int displayCamelotKey() const { return mode.modal ? mode.camelotKeyIndex() : majorMinorKey; }
    [[nodiscard]] int tonic() const { return mode.modal ? mode.tonic : (majorMinorKey < 0 ? -1 : keyTonic(majorMinorKey)); }
    // The scale to show and tune to: the mode, major, or for a minor key natural, harmonic or melodic minor. Harmonic
    // when the raised 7th is at least 3 % of the notes and outweighs the flat 7th; melodic when the raised 6th does the
    // same over the flat 6th as well.
    [[nodiscard]] TuningScale tuningScale() const
    {
        TuningScale t;
        if (! valid)
            return t;
        if (mode.modal)
        {
            t.tonic = mode.tonic;
            t.type = modeName(mode.mode);
            t.notes = mode.scale();
            return t;
        }
        t.tonic = keyTonic(majorMinorKey);
        std::array<int, 7> iv = modeIntervals(keyIsMinor(majorMinorKey) ? Mode::aeolian : Mode::ionian);
        t.type = keyIsMinor(majorMinorKey) ? "Natural Minor" : "Major";
        if (keyIsMinor(majorMinorKey))
        {
            const auto share = [&](int step) { return presence[static_cast<std::size_t>((t.tonic + step) % 12)]; };
            constexpr double minShare = 0.03;
            if (share(11) >= minShare && share(11) > share(10))
            {
                iv[6] = 11;
                t.type = "Harmonic Minor";
                if (share(9) >= minShare && share(9) > share(8))
                {
                    iv[5] = 9;
                    t.type = "Melodic Minor";
                }
            }
        }
        for (int i = 0; i < 7; ++i)
            t.notes[static_cast<std::size_t>(i)] = (t.tonic + iv[static_cast<std::size_t>(i)]) % 12;
        return t;
    }
    // The 7 notes of the song's scale (as tuningScale()).
    [[nodiscard]] std::array<int, 7> scale() const { return tuningScale().notes; }
};

class KeyAnalyzer
{
public:
    void prepare(double sampleRate);
    void clear();

    // Feed audio: mono (channel average) samples, the host position of the first sample, and the block's energy
    // (sum of squares over all channels) for the 30 s block loudness. When `accumulate` is false the audio still drives
    // the local key and chord but the timeline map is left alone.
    void process(const float* mono, int numSamples, const HostPosition& position, double energy, int numChannels, bool accumulate);
    // Offline use: a clip shorter than one frame still gets one zero-padded frame (as KeyDetector does).
    void finish();

    [[nodiscard]] KeySummary computeSummary() const;
    [[nodiscard]] int localKey() const { return smoothedLocalKey; }
    [[nodiscard]] int currentChord() const { return chordNow; }
    [[nodiscard]] const std::map<int, TimelineBin>& bins() const { return timeline; }
    [[nodiscard]] double currentBeat() const { return lastBeat; }
    [[nodiscard]] bool usesHostTimeline() const { return hostTimeline; }

    // ARA: copy another analyser's map (an audio source analysed offline) into this one, its bins shifted by
    // `indexOffset` and their host time by `secondsOffset`, so several sources sit one after another.
    // Only bins minIndex..maxIndex are taken; bar positions are dropped (an ARA source has no bars).
    void importBins(const KeyAnalyzer& other, int indexOffset, double secondsOffset, int minIndex = INT_MIN, int maxIndex = INT_MAX);
    [[nodiscard]] double getSampleRate() const { return sampleRate; }

    void writeTo(juce::OutputStream&) const;
    bool readFrom(juce::InputStream&);

    static constexpr double freeRunBinSeconds = 0.5; // bin length without a host timeline

private:
    struct Marker
    {
        juce::int64 sample;
        HostPosition position;
        int pass;
        bool accumulate;
    };

    void analyseFrame(juce::int64 frameStart, int validSamples);
    [[nodiscard]] const Marker* markerFor(juce::int64 sample) const;
    [[nodiscard]] double beatAt(const Marker&, juce::int64 sample) const;
    [[nodiscard]] double secondsAt(const Marker&, juce::int64 sample) const;
    [[nodiscard]] int binIndexFor(double beat) const;
    void beginPassIfJumped(int bin, bool playing);
    void updateLocalKeyAndChord(const Chroma& frameChroma, const Chroma& frameBass);
    [[nodiscard]] double percussionMask(double midi) const;
    [[nodiscard]] bool isOvertone(const Peak&) const;

    double sampleRate = 48000.0;
    KeyFrameAnalyzer frameAnalyzer;
    std::vector<float> ring;          // the last frameSize samples
    juce::int64 samplesIn = 0;        // samples received so far
    juce::int64 nextFrameStart = 0;
    std::deque<Marker> markers;
    std::vector<Peak> peaks, lowPeaks;
    std::vector<float> frameBuffer;

    std::map<int, TimelineBin> timeline;
    int pass = 0;
    int lastBin = INT_MIN;
    bool wasPlaying = false;
    bool hostTimeline = false;
    double freeRunSeconds = 0.0;
    double lastBeat = 0.0;

    // Local key / chord: recent frames, folded with a running tuning estimate.
    std::deque<Chroma> recentFrames;
    std::deque<Chroma> recentBass;    // the same frames' bass (below ~250 Hz, down to 35 Hz), for the chord root and tonic
    std::deque<int> recentChords;     // the chord heard at each of those frames
    double runRe = 0.0, runIm = 0.0;
    int rawLocalKey = -1, smoothedLocalKey = -1;
    int chordNow = -1;
    OnlineViterbi keyHmm, chordHmm; // 24 keys; 12 roots x 5 chord qualities
};

// Folds a fine (10-cent) chroma into 12 pitch classes after taking off `tuneOffset` semitones, soft-assigning each
// fine bin across its two nearest pitch classes (KeyDetector's last step).
void foldFine(const std::array<float, TimelineBin::fineBins>& fine, double tuneOffset, Chroma& out);
} // namespace rk
