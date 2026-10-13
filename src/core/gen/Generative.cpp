#include "gen/Generative.h"
#include <algorithm>
#include <cmath>

namespace mnm::gen {

namespace {

int64_t floorDiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

// Purposes of the hashed decisions, so no two decisions share a random number.
enum Salt : uint64_t {
    kProb = 1, kHold, kPitch, kWalk, kWalkInit, kTuringInit, kTuringFlip, kCycle, kCycleMut, kCycleMutVal,
    kRatchet, kRatchetN, kRatchetShape, kDrift, kEvolve,
    kLinkChord = 50, kLinkLift, kLinkScramble, kLinkScrambleNote, kLinkDrift = 60,   // (Link: after the song's own, so those keep their numbers)
    kLaneChance = 100, kLaneValue = 200, kLaneCycle = 300, kLaneTuring = 400
};

uint64_t mix(uint64_t x)   // splitmix64 finaliser
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Position of ratchet i of n inside the step (0..1) for a timing shape.
double shapeAt(Shape s, double x)
{
    switch (s) {
    case Shape::Accel: return 1.0 - (1.0 - x) * (1.0 - x);   // the repeats get closer: a roll that speeds up
    case Shape::Decel: return x * x;                         // a bouncing ball: the gaps grow
    default: return x;
    }
}

} // namespace

uint64_t hash64(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
    uint64_t h = mix(a);
    h = mix(h ^ b); h = mix(h ^ c); h = mix(h ^ d); h = mix(h ^ e);
    return h;
}

bool euclidHit(int steps, int pulses, int rotate, int i)
{
    if (steps <= 0) return false;
    pulses = std::clamp(pulses, 0, steps);
    if (pulses == 0) return false;
    const int j = int(floorMod(int64_t(i) - rotate, steps));
    // Bresenham: a pulse where the running count of pulses*j/steps steps up; j = 0 is always a pulse
    return (j * pulses) % steps < pulses;
}

int scaleNotes(int root, int scale, int low, int high, uint8_t* out)
{
    const uint16_t mask = kScales[std::clamp(scale, 0, kNumScales - 1)].mask;
    low = std::clamp(low, 0, 127); high = std::clamp(high, 0, 127);
    if (high < low) std::swap(low, high);
    int n = 0;
    for (int note = low; note <= high; ++note)
        if ((mask >> int(floorMod(note - root, 12))) & 1) out[n++] = uint8_t(note);
    return n;
}

Generator::Generator()
{
    for (auto& u : ui) for (auto& l : u.lock) l.store(-1);
    reset();
}

void Generator::reset()
{
    for (int t = 0; t < kTracks; ++t) {
        auto& st = m_tracks[size_t(t)];
        st.count = 0;
        st.lastPlayed = false;
        st.degree = -1;
        st.turingSet = false;
        st.lastNote = -1;
        st.trigCount = 0;
        st.gen.fill(0);
        st.genCycle.fill(0);
        st.sounding = false;
        ui[size_t(t)].step.store(-1);
    }
    m_wasPlaying = false;
}

void Generator::emit(int sample, uint8_t prio, std::initializer_list<uint8_t> bytes)
{
    if (m_numEvents >= kMaxEvents) return;
    auto& e = m_events[size_t(m_numEvents++)];
    e.sample = sample; e.prio = prio; e.size = uint8_t(bytes.size());
    std::copy(bytes.begin(), bytes.end(), e.data);
}

void Generator::push(int t, double time, uint8_t prio, std::initializer_list<uint8_t> bytes)
{
    auto& st = m_tracks[size_t(t)];
    if (st.count >= int(st.queue.size())) return;
    auto& p = st.queue[size_t(st.count++)];
    p.time = time; p.prio = prio; p.size = uint8_t(bytes.size());
    std::copy(bytes.begin(), bytes.end(), p.data);
}

void Generator::allNotesOff(int sample, const GlobalParams& gp)
{
    for (int t = 0; t < kTracks; ++t) {
        if (!gp.tracks[size_t(t)].on && !m_tracks[size_t(t)].sounding) continue;   // (an FX machine on an idle track stays trigged)
        emit(sample, 0, {uint8_t(0xB0 | t), 123, 0});                          // the track's notes stop
        emit(sample, 0, {0xF0, kSysexId, kLockClear, uint8_t(t), 0xF7});        // its knobs go back to their values
        m_tracks[size_t(t)].sounding = false;
    }
}

int Generator::pickNote(int t, TrackState& st, const TrackParams& p, const GlobalParams& gp, uint64_t key, const uint8_t* notes, int n, int& idx)
{
    (void)t;
    idx = 0;
    if (p.pitchMode == PitchMode::Fixed || n == 0) {   // drums: the LOW note as it is, never quantised or transposed
        const int note = std::clamp(p.low, 0, 127);
        for (int i = 0; i < n; ++i) if (notes[i] >= note) { idx = i; break; }
        return note;
    }
    // HOLD: the previous note again
    if (st.lastNote >= 0 && unit(hash64(key, kHold)) * 100.0 < p.hold) {
        for (int i = 0; i < n; ++i) if (notes[i] >= st.lastNote) { idx = i; return notes[i]; }
        idx = n - 1;
        return notes[n - 1];
    }
    double v = 0.0;
    switch (p.pitchMode) {
    case PitchMode::Random:
        v = unit(hash64(key, kPitch));
        break;
    case PitchMode::Walk: {
        if (st.degree < 0 || st.degree >= n) st.degree = int(unit(hash64(key, kWalkInit)) * n);
        const int jump = std::clamp(p.length, 1, 12);
        const int d = int(unit(hash64(key, kWalk)) * (2 * jump + 1)) - jump;
        int g = st.degree + d;
        if (n == 1) g = 0;
        else {   // reflect at the ends of the range, so the walk stays inside it
            while (g < 0 || g >= n) g = g < 0 ? -g : 2 * (n - 1) - g;
        }
        st.degree = g;
        idx = g;
        return notes[g];
    }
    case PitchMode::Turing: {
        const int len = std::clamp(p.length, 2, 16);
        v = double(st.turing & ((1u << len) - 1u)) / double(1u << len);
        break;
    }
    case PitchMode::Cycle: {
        const int len = std::clamp(p.length, 1, 32);
        const int64_t i = floorMod(st.trigCount, len);
        v = unit(hash64(uint64_t(gp.seed), uint64_t(t), kCycle, uint64_t(i)));
        if (unit(hash64(key, kCycleMut)) * 100.0 < p.mutate) v = unit(hash64(key, kCycleMutVal));
        break;
    }
    default: break;
    }
    idx = std::clamp(int(v * n), 0, n - 1);
    return notes[idx];
}

int nearestChordNote(int note, uint16_t chordMask, int low, int high)
{
    if ((chordMask & 0xFFF) == 0) return note;
    int outside = -1;
    for (int d = 0; d <= 6; ++d)
        for (int sgn : {-1, 1}) {   // (down first: a tie keeps the line from climbing)
            const int c = note + sgn * d;
            if (c < 0 || c > 127 || !((chordMask >> (c % 12)) & 1)) continue;
            if (c >= low && c <= high) return c;
            if (outside < 0) outside = c;
        }
    return outside >= 0 ? outside : note;
}

void Generator::decideStep(int t, int64_t s, double q, const Transport& tr, const GlobalParams& gp)
{
    auto& st = m_tracks[size_t(t)];
    const auto& p = gp.tracks[size_t(t)];
    auto& u = ui[size_t(t)];
    const int steps = std::clamp(p.steps, 1, kMaxSteps);
    // LOOP folds the position: the same loop step makes the same decisions every time round (until EVOLVE), and
    // every pattern restarts with the loop, so a 13-step track in a 1-bar loop really plays the same bar again
    int64_t ls = s, cycle = 0, cyclesPerLoop = 0;
    if (gp.loop > 0) {
        const int64_t loopSteps = std::max<int64_t>(1, std::llround(kLoopBars[std::clamp(gp.loop, 0, kNumLoops - 1)] * tr.barQuarters / q));
        ls = floorMod(s, loopSteps);
        cycle = floorDiv(s, loopSteps);
        cyclesPerLoop = (loopSteps + steps - 1) / steps;
        if (ls == 0) { st.degree = -1; st.turingSet = false; st.trigCount = 0; st.lastNote = -1; }
    }
    const int pos = int(floorMod(ls, steps));
    u.step.store(pos);
    const size_t slot = size_t(floorMod(ls, kEvolveSlots));
    if (gp.loop > 0 && cycle != st.genCycle[slot]) {
        st.genCycle[slot] = cycle;
        if (gp.evolve > 0 && unit(hash64(uint64_t(gp.seed), uint64_t(t), kEvolve, uint64_t(ls), uint64_t(cycle))) * 100.0 < gp.evolve) ++st.gen[slot];
    }
    const uint64_t key = hash64(uint64_t(gp.seed) * 7919u + 17u, uint64_t(t), uint64_t(ls), st.gen[slot]);

    const bool gridMode = p.mode == TrackMode::Grid;
    const uint32_t cell = gridMode ? p.grid[size_t(pos)] : 0u;
    // the link's DRIFT: the pattern wanders (from the seed and the bar, so a position always plays the same)
    int pulses = p.pulses, rotate = p.rotate;
    double driftThin = 0.0;
    if (gp.link.drift > 0.0f && !gridMode) {
        const int64_t bar = int64_t(std::floor(double(s) * q / std::max(0.25, tr.barQuarters) + 1e-9));
        const int64_t seg = floorDiv(bar, 4);
        const double f = double(bar - seg * 4) / 4.0;
        auto wander = [&](uint64_t k) {
            const double a = unit(hash64(uint64_t(gp.seed), uint64_t(t), kLinkDrift + k, uint64_t(seg))) * 2.0 - 1.0;
            const double b = unit(hash64(uint64_t(gp.seed), uint64_t(t), kLinkDrift + k, uint64_t(seg + 1))) * 2.0 - 1.0;
            return a + (b - a) * f;
        };
        const double a = gp.link.drift;
        if (pulses > 0) pulses = std::clamp(pulses + int(std::lround(1.5 * a * wander(0))), 1, steps);
        rotate += int(std::lround(3.0 * a * wander(1)));
        driftThin = 0.25 * a * (wander(2) + 1.0) * 0.5;
    }
    if (gridMode ? !gridTrig(cell) : (!euclidHit(steps, pulses, rotate, pos) || ((p.stepMask >> pos) & 1u))) return;

    // trig condition
    // X:Y counts the pattern's repetitions through the song (inside a loop: across loops too)
    const int64_t patternCycle = gp.loop > 0 ? cycle * cyclesPerLoop + floorDiv(ls, steps) : floorDiv(s, steps);
    auto xy = [&](int x, int y) { return floorMod(patternCycle, y) == x - 1; };
    const auto& neighbour = m_tracks[size_t((t + kTracks - 1) % kTracks)];
    bool ok = true;
    switch (p.cond) {
    case Cond::P1_2: ok = xy(1, 2); break; case Cond::P2_2: ok = xy(2, 2); break;
    case Cond::P1_3: ok = xy(1, 3); break; case Cond::P2_3: ok = xy(2, 3); break; case Cond::P3_3: ok = xy(3, 3); break;
    case Cond::P1_4: ok = xy(1, 4); break; case Cond::P2_4: ok = xy(2, 4); break; case Cond::P3_4: ok = xy(3, 4); break; case Cond::P4_4: ok = xy(4, 4); break;
    case Cond::Pre: ok = st.lastPlayed; break; case Cond::NotPre: ok = !st.lastPlayed; break;
    case Cond::Nei: ok = neighbour.lastPlayed; break; case Cond::NotNei: ok = !neighbour.lastPlayed; break;
    case Cond::First: ok = (gp.loop > 0 ? cycle : floorDiv(s, steps)) == 0; break;
    case Cond::NotFirst: ok = (gp.loop > 0 ? cycle : floorDiv(s, steps)) != 0; break;
    default: break;
    }
    // the link (MAKU): SINK thins the tracks (the drums the most), SCRAMBLE drops a few hits
    const Link& lk = gp.link;
    double prob = p.prob;
    if (lk.sink > 0.0f) prob *= 1.0 - double(lk.sink) * (p.pitchMode == PitchMode::Fixed ? 0.9 : 0.5);
    if (lk.scramble > 0.0f) prob *= 1.0 - 0.3 * double(lk.scramble);
    prob *= 1.0 - driftThin;
    const bool played = ok && unit(hash64(key, kProb)) * 100.0 < prob;
    st.lastPlayed = played;
    if (!played) return;

    const bool accent = !gridMode && p.accents > 0 && euclidHit(steps, p.accents, rotate, pos);

    // the TURING register moves on every trig (pitch and lanes read it)
    {
        if (!st.turingSet) { st.turing = uint16_t(hash64(uint64_t(gp.seed), uint64_t(t), kTuringInit) & 0xFFFF); st.turingSet = true; }
        const int len = std::clamp(p.length, 2, 16);
        uint16_t bit = uint16_t((st.turing >> (len - 1)) & 1u);
        if (unit(hash64(key, kTuringFlip)) * 100.0 < p.mutate) bit ^= 1u;
        st.turing = uint16_t(((st.turing << 1) | bit) & ((1u << len) - 1u));
    }

    // pitch, inside the range and the scale (moved by the key transposition)
    uint8_t notes[128];
    const int tp = p.pitchMode == PitchMode::Fixed ? 0 : m_transpose;
    const int n = scaleNotes(gp.root + tp, p.scale >= 0 ? p.scale : gp.scale, p.low + tp, p.high + tp, notes);
    int idx = 0;
    int note = 0;
    if (gridMode) {   // the written note, moved by the key transposition
        note = std::clamp(gridNote(cell) + m_transpose, 0, 127);
        idx = std::clamp(int(std::lround((note - 24) / 72.0 * std::max(1, n - 1))), 0, std::max(0, n - 1));
    } else {
        note = pickNote(t, st, p, gp, key, notes, n, idx);
        if (p.pitchMode != PitchMode::Fixed && n > 0) {   // the link: SCRAMBLE, the chord, LIFT
            if (lk.scramble > 0.0f && unit(hash64(key, kLinkScramble)) < 0.6 * double(lk.scramble)) {
                idx = std::clamp(int(unit(hash64(key, kLinkScrambleNote)) * n), 0, n - 1);
                note = notes[idx];
            }
            if (lk.chordMask && lk.chord > 0 && unit(hash64(key, kLinkChord)) * 100.0 < lk.chord)
                note = nearestChordNote(note, lk.chordMask, p.low + tp, p.high + tp);
            if (lk.lift > 0.0f && note + 12 <= 127 && unit(hash64(key, kLinkLift)) < 0.5 * double(lk.lift)) note += 12;
        }
    }
    st.lastNote = note;

    // ratchets
    // (the link: CASCADE rolls the melodic tracks upward, SCRAMBLE rolls everything)
    const bool melodic = !gridMode && p.pitchMode != PitchMode::Fixed;
    double rChance = p.ratchetChance;
    int rMax = p.ratchetMax, rPitch = p.ratchetPitch;
    if (lk.cascade > 0.0f && melodic) { rChance += 60.0 * double(lk.cascade); rMax = std::max(rMax, 2 + int(std::lround(3.0 * lk.cascade))); if (rPitch == 0) rPitch = 1; }
    if (lk.scramble > 0.0f) { rChance += 50.0 * double(lk.scramble); rMax = std::max(rMax, 2 + int(std::lround(2.0 * lk.scramble))); }
    int count = 1;
    if (rMax >= 2 && unit(hash64(key, kRatchet)) * 100.0 < rChance)
        count = 2 + std::min(rMax - 2, int(unit(hash64(key, kRatchetN)) * (rMax - 1)));
    Shape shape = lk.scramble > 0.5f ? Shape::Random : p.ratchetShape;
    if (shape == Shape::Random) shape = Shape(int(unit(hash64(key, kRatchetShape)) * 3.0));

    // timing: swing delays every second step, drift a random amount (both up to half a step)
    const double drift = std::min(100.0, p.drift + 40.0 * double(lk.wobble) + 30.0 * double(lk.scramble));   // (WOBBLE, SCRAMBLE)
    const double delay = ((pos & 1) ? p.swing / 100.0 * 0.5 * q : 0.0) + drift / 100.0 * 0.5 * q * unit(hash64(key, kDrift));
    const double t0 = double(s) * q + delay;

    // parameter lanes: the locks this trig sets (the track's previous locks are cleared first)
    const uint8_t ch = uint8_t(t);
    push(t, t0, 2, {0xF0, kSysexId, kLockClear, ch, 0xF7});
    for (int l = 0; l < kLanes; ++l) {
        const auto& lp = p.lanes[size_t(l)];
        if (lp.target < 0 || lp.target >= kLockTargets || unit(hash64(key, kLaneChance + uint64_t(l))) * 100.0 >= lp.chance) {
            u.lock[l].store(-1);
            continue;
        }
        const int len = std::clamp(lp.length, 1, 64);
        const double ph = double(floorMod(st.trigCount, len));
        double x = 0.0;
        switch (lp.mode) {
        case LaneMode::Random:   x = unit(hash64(key, kLaneValue + uint64_t(l))); break;
        case LaneMode::Cycle:    x = unit(hash64(uint64_t(gp.seed), uint64_t(t), kLaneCycle + uint64_t(l), uint64_t(ph))); break;
        case LaneMode::RampUp:   x = len > 1 ? ph / (len - 1) : 1.0; break;
        case LaneMode::RampDown: x = len > 1 ? 1.0 - ph / (len - 1) : 0.0; break;
        case LaneMode::Triangle: x = len > 1 ? 1.0 - std::abs(2.0 * ph / len - 1.0) : 1.0; break;
        case LaneMode::Sine:     x = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * double(floorMod(ls, len)) / len); break;
        case LaneMode::Accent:   x = accent ? 1.0 : 0.0; break;
        case LaneMode::Pitch:    x = gridMode ? std::clamp((note - 24) / 72.0, 0.0, 1.0) : n > 1 ? double(idx) / double(n - 1) : 0.5; break;
        case LaneMode::Turing:   x = double(hash64(st.turing, uint64_t(l), kLaneTuring) & 0xFFFF) / 65535.0; break;
        default: break;
        }
        const int v = std::clamp(int(std::lround(lp.min + (lp.max - lp.min) * x)), 0, 127);
        push(t, t0, 2, {0xF0, kSysexId, kLockSet, ch, uint8_t(lp.target), uint8_t(v), 0xF7});
        u.lock[l].store(v);
    }

    // the notes: count repeats spread over one step from t0, each gated. A GRID step holds its note for its own
    // length (it may tie over the next steps: a mono voice then glides legato into the next note)
    double gate = std::clamp(p.gate, 1, 100) / 100.0;
    gate += (1.0 - gate) * double(lk.swell);   // (SWELL: towards legato)
    const double written = gridMode ? gridLength(cell) / 4.0 * q : 0.0;
    const double minLen = std::min(0.004, q * 0.25);
    for (int i = 0; i < count; ++i) {
        const double a = t0 + q * shapeAt(shape, double(i) / count);
        const double b = t0 + q * shapeAt(shape, double(i + 1) / count);
        int nn = note;
        if (rPitch != 0 && i > 0) {
            if (gridMode || p.pitchMode == PitchMode::Fixed || n == 0) nn = std::clamp(note + i * rPitch, 0, 127);
            else nn = std::clamp(notes[std::clamp(idx + i * rPitch, 0, n - 1)] + (note - notes[std::clamp(idx, 0, n - 1)]), 0, 127);
        }
        const double off = gridMode ? (count == 1 ? t0 + std::max(minLen, written - 1e-4) : std::min(b, a + std::max(minLen, (b - a) * 0.9)))
                                    : std::min(b, a + std::max(minLen, (b - a) * gate));
        push(t, a, 3, {uint8_t(0x90 | ch), uint8_t(nn), 100});
        st.sounding = true;
        push(t, off, 1, {uint8_t(0x80 | ch), uint8_t(nn), 0});
    }
    st.trigCount++;
    u.note.store(note);
    u.ratchets.store(count);
    u.trigs.fetch_add(1);
}

void Generator::process(const Transport& tr, const GlobalParams& gp)
{
    m_numEvents = 0;
    if (!tr.playing || tr.numSamples <= 0 || tr.bpm <= 0.0 || tr.sampleRate <= 0.0) {
        if (m_wasPlaying) { allNotesOff(0, gp); reset(); }
        uiPlaying.store(false);
        return;
    }
    const double pps = tr.bpm / (60.0 * tr.sampleRate);   // quarters per sample
    const double ppq1 = tr.ppq + tr.numSamples * pps;

    // a start or a jump of the play position (a DAW loop, a locate): everything scheduled is dropped and the tracks
    // pick up at the new position; the evolution of the loop is kept
    const bool jumped = !m_wasPlaying || std::abs(tr.ppq - m_expectedPpq) > std::max(8.0 * pps, 1e-3);
    if (jumped) {
        if (m_wasPlaying) allNotesOff(0, gp);
        for (auto& st : m_tracks) { st.count = 0; st.nextPpq = tr.ppq - 1e-4; st.lastPlayed = false; }
    }
    m_wasPlaying = true;
    m_expectedPpq = ppq1;
    uiPlaying.store(true);
    uiPpq.store(tr.ppq);

    for (int t = 0; t < kTracks; ++t) {
        auto& st = m_tracks[size_t(t)];
        const auto& p = gp.tracks[size_t(t)];
        if (!p.on) { st.nextPpq = ppq1; ui[size_t(t)].step.store(-1); continue; }
        const double q = kRateQuarters[std::clamp(p.rate, 0, kNumRates - 1)];
        int64_t s = int64_t(std::ceil(st.nextPpq / q - 1e-9));
        for (int guard = 0; double(s) * q < ppq1 && guard < 256; ++guard, ++s) decideStep(t, s, q, tr, gp);
        st.nextPpq = double(s) * q;
    }

    // the events due in this block, in time order
    for (int t = 0; t < kTracks; ++t) {
        auto& st = m_tracks[size_t(t)];
        int keep = 0;
        for (int i = 0; i < st.count; ++i) {
            const auto& p = st.queue[size_t(i)];
            if (p.time < ppq1) {
                const int sample = std::clamp(int((p.time - tr.ppq) / pps), 0, tr.numSamples - 1);
                if (m_numEvents < kMaxEvents) {
                    auto& e = m_events[size_t(m_numEvents++)];
                    e.sample = sample; e.prio = p.prio; e.size = p.size;
                    std::copy(p.data, p.data + p.size, e.data);
                }
            } else {
                st.queue[size_t(keep++)] = p;
            }
        }
        st.count = keep;
    }
    // insertion sort, stable, no allocation (std::stable_sort may allocate on the audio thread)
    for (int i = 1; i < m_numEvents; ++i) {
        const Event e = m_events[size_t(i)];
        int j = i - 1;
        while (j >= 0 && (m_events[size_t(j)].sample > e.sample || (m_events[size_t(j)].sample == e.sample && m_events[size_t(j)].prio > e.prio))) {
            m_events[size_t(j + 1)] = m_events[size_t(j)];
            --j;
        }
        m_events[size_t(j + 1)] = e;
    }
}

} // namespace mnm::gen
