#include "ModeDetector.h"

namespace rk
{
namespace
{
Chroma normalisedToMax(const Chroma& c)
{
    double m = 0.0;
    for (auto v : c)
        m = std::max(m, v);
    Chroma out {};
    if (m > 0.0)
        for (int i = 0; i < 12; ++i)
            out[static_cast<std::size_t>(i)] = c[static_cast<std::size_t>(i)] / m;
    return out;
}

double pearson(const Chroma& a, const double* b)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i)
    {
        ma += a[static_cast<std::size_t>(i)] / 12.0;
        mb += b[i] / 12.0;
    }
    double n = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double x = a[static_cast<std::size_t>(i)] - ma, y = b[i] - mb;
        n += x * y;
        da += x * x;
        db += y * y;
    }
    return (da > 1e-12 && db > 1e-12) ? n / std::sqrt(da * db) : 0.0;
}

// The characteristic note of each mode and the note it replaces in the same-tonic major or minor, as semitones
// above the tonic. Ionian / Aeolian have none.
std::pair<int, int> characteristicOf(Mode m)
{
    switch (m)
    {
        case Mode::dorian:     return { 9, 8 };   // major 6th instead of minor's flat 6th
        case Mode::phrygian:   return { 1, 2 };   // flat 2nd instead of the 2nd
        case Mode::lydian:     return { 6, 5 };   // raised 4th instead of the 4th
        case Mode::mixolydian: return { 10, 11 }; // flat 7th instead of major's 7th
        case Mode::locrian:    return { 6, 7 };   // flat 5th instead of the 5th
        default:               return { -1, -1 };
    }
}
} // namespace

ModeResult detectMode(const ModeInputs& in, const ModeRules& rules)
{
    ModeResult r;
    const Chroma chroma = normalisedToMax(in.chroma);
    double total = 0.0;
    for (auto v : chroma)
        total += v;
    if (total <= 0.0)
        return r;

    // 1. The pitch collection: the best of the 12 diatonic sets (binary templates, Pearson).
    static const double diatonic[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
    double bestFit = -2.0;
    for (int m = 0; m < 12; ++m)
    {
        Chroma rotated {};
        for (int i = 0; i < 12; ++i)
            rotated[static_cast<std::size_t>(i)] = chroma[static_cast<std::size_t>((i + m) % 12)];
        const double fit = pearson(rotated, diatonic);
        if (fit > bestFit)
        {
            bestFit = fit;
            r.collection = m;
        }
    }

    // 2. The tonic: score each of the collection's 7 notes from the bass, chord roots, its own weight, the support of
    //    its fifth (when the fifth is in the collection) and agreement with NuvoDeck's tonic. With a host timeline the
    //    chords on downbeats and at phrase starts / ends count too (songs tend to start and close phrases on the tonic);
    //    without one the weights stay as they were.
    const Chroma bass = normalisedToMax(in.bass), roots = normalisedToMax(in.chordRoots), phrase = normalisedToMax(in.phraseRoots);
    double phraseTotal = 0.0;
    for (auto v : phrase)
        phraseTotal += v;
    const bool usePhrase = phraseTotal > 0.0;
    const double wBass = usePhrase ? 0.30 : 0.35, wRoots = usePhrase ? 0.25 : 0.30, wPhrase = usePhrase ? 0.10 : 0.0;
    static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    std::array<double, 7> score {};
    for (int d = 0; d < 7; ++d)
    {
        const int t = (r.collection + majorSteps[d]) % 12;
        const int fifth = (t + 7) % 12;
        const bool fifthInSet = diatonic[(fifth - r.collection + 12) % 12] > 0.5;
        score[static_cast<std::size_t>(d)] = wBass * bass[static_cast<std::size_t>(t)]
                                           + wRoots * roots[static_cast<std::size_t>(t)]
                                           + wPhrase * phrase[static_cast<std::size_t>(t)]
                                           + 0.20 * chroma[static_cast<std::size_t>(t)]
                                           + 0.10 * (fifthInSet ? chroma[static_cast<std::size_t>(fifth)] : 0.0)
                                           + 0.05 * ((in.nuvoKey >= 0 && keyTonic(in.nuvoKey) == t) ? 1.0 : 0.0);
    }
    int best = 0, second = 1;
    for (int d = 0; d < 7; ++d)
        if (score[static_cast<std::size_t>(d)] > score[static_cast<std::size_t>(best)])
            best = d;
    second = best == 0 ? 1 : 0;
    for (int d = 0; d < 7; ++d)
        if (d != best && score[static_cast<std::size_t>(d)] > score[static_cast<std::size_t>(second)])
            second = d;

    // 3. The mode is where the tonic sits in the collection.
    r.mode = static_cast<Mode>(best);
    r.tonic = (r.collection + majorSteps[best]) % 12;
    r.margin = score[static_cast<std::size_t>(best)] - score[static_cast<std::size_t>(second)];
    r.tonicScores = score;

    // 4. The reporting rule: margin, characteristic note present, the note it replaces absent.
    const auto [charStep, replacedStep] = characteristicOf(r.mode);
    if (charStep < 0)
        return r; // Ionian / Aeolian: NuvoDeck's major / minor answer stands
    r.characteristicPc = (r.tonic + charStep) % 12;
    r.replacedPc = (r.tonic + replacedStep) % 12;
    r.characteristicShare = in.presence[static_cast<std::size_t>(r.characteristicPc)];
    r.replacedShare = in.presence[static_cast<std::size_t>(r.replacedPc)];
    // The replaced note may leak in as an overtone (Locrian's missing perfect fifth is the tonic's 3rd harmonic), so
    // it only has to be rare next to the characteristic note: under 1 %, or under 12 % of the characteristic note.
    const double replacedLimit = std::max(rules.maxReplaced, 0.12 * r.characteristicShare);
    if (r.margin >= rules.minMargin && r.characteristicShare >= rules.minCharacteristic && r.replacedShare < replacedLimit)
        r.modal = true;
    else if (r.margin >= rules.minMargin * 0.4 && r.characteristicShare >= rules.minCharacteristic * 0.66)
        r.hint = true;
    return r;
}
} // namespace rk
