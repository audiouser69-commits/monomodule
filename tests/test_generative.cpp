// Generative Euclidean sequencer (src/core/gen/Generative.cpp): rhythms, scales, determinism, ratchets, locks and
// transport handling, without the DSP (no OS file needed).
#include "mini_test.h"
#include "gen/Generative.h"
#include <cmath>
#include <map>
#include <vector>
using namespace mnm::gen;

namespace {
struct Ev { double ppq; int status, d1, d2; bool sysex; int track, idx, value; };

// Plays `quarters` quarter notes in blocks of `block` samples at 120 BPM / 48 kHz from ppq `start`.
std::vector<Ev> run(Generator& g, const GlobalParams& gp, double quarters, int block = 512, double start = 0.0)
{
    std::vector<Ev> out;
    Transport tr; tr.playing = true; tr.bpm = 120.0; tr.sampleRate = 48000.0; tr.numSamples = block;
    const double pps = tr.bpm / (60.0 * tr.sampleRate);
    for (double ppq = start; ppq < start + quarters; ppq += block * pps) {
        tr.ppq = ppq;
        g.process(tr, gp);
        for (int i = 0; i < g.numEvents(); ++i) {
            const auto& e = g.event(i);
            Ev v{ppq + e.sample * pps, e.data[0], e.data[1], e.data[2], e.data[0] == 0xF0, -1, -1, -1};
            if (v.sysex) { v.track = e.data[3]; if (e.data[2] == kLockSet) { v.idx = e.data[4]; v.value = e.data[5]; } }
            if (v.ppq < start + quarters - 1e-3) out.push_back(v);   // the last block may run past the end
        }
    }
    return out;
}
std::vector<Ev> noteOns(const std::vector<Ev>& ev, int track = -1)
{
    std::vector<Ev> r;
    for (const auto& e : ev) if (!e.sysex && (e.status & 0xF0) == 0x90 && (track < 0 || (e.status & 15) == track)) r.push_back(e);
    return r;
}
GlobalParams oneTrack()
{
    GlobalParams gp;
    auto& t = gp.tracks[0];
    t.on = true; t.steps = 16; t.pulses = 4; t.rate = 4; t.pitchMode = PitchMode::Fixed; t.low = 36;
    return gp;
}
} // namespace

TEST_CASE(gen_euclid_patterns)
{
    // E(3,8) = x..x..x. ; E(5,8) = x.xx.xx. up to rotation, five pulses; E(4,16) = four on the floor
    std::string p38, p48;
    for (int i = 0; i < 8; ++i) p38 += euclidHit(8, 3, 0, i) ? 'x' : '.';
    CHECK_EQ(p38, std::string("x..x..x."));
    int c = 0; for (int i = 0; i < 8; ++i) c += euclidHit(8, 5, 0, i);
    CHECK_EQ(c, 5);
    for (int i = 0; i < 16; ++i) CHECK_EQ(euclidHit(16, 4, 0, i), i % 4 == 0);
    // rotation moves the pattern right
    for (int i = 0; i < 8; ++i) CHECK_EQ(euclidHit(8, 3, 1, i), euclidHit(8, 3, 0, (i + 7) % 8));
    CHECK(!euclidHit(8, 0, 0, 0));
    for (int i = 0; i < 8; ++i) CHECK(euclidHit(8, 99, 0, i));   // pulses clamp to steps
}

TEST_CASE(gen_scales)
{
    uint8_t n[128];
    CHECK_EQ(scaleNotes(0, 1, 60, 72, n), 8);   // C major C4..C5
    CHECK_EQ(int(n[0]), 60); CHECK_EQ(int(n[1]), 62); CHECK_EQ(int(n[2]), 64); CHECK_EQ(int(n[7]), 72);
    CHECK_EQ(scaleNotes(9, 11, 57, 69, n), 6);  // A minor pentatonic A3..A4: A C D E G A
    CHECK_EQ(int(n[1]), 60); CHECK_EQ(int(n[4]), 67);
    CHECK_EQ(scaleNotes(0, 21, 30, 70, n), 3);  // OCTAVES: 36 48 60
}

TEST_CASE(gen_four_on_the_floor_locked_to_transport)
{
    Generator g;
    auto ev = run(g, oneTrack(), 8.0);
    auto ons = noteOns(ev);
    CHECK_EQ(int(ons.size()), 8);   // 4 per bar of 16 sixteenths, two bars
    for (size_t i = 0; i < ons.size(); ++i) {
        CHECK(std::abs(ons[i].ppq - double(i)) < 0.001);   // on the beats, sample-accurate
        CHECK_EQ(ons[i].d1, 36);
    }
    // every note is released before the next one (gate 50% of a sixteenth)
    int offs = 0; for (const auto& e : ev) if (!e.sysex && (e.status & 0xF0) == 0x80) ++offs;
    CHECK_EQ(offs, 8);
}

TEST_CASE(gen_block_size_independent_and_deterministic)
{
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.pulses = 7; t.pitchMode = PitchMode::Random; t.low = 40; t.high = 80; t.prob = 70; t.ratchetChance = 40; t.ratchetMax = 4;
    t.drift = 30; t.swing = 20;
    t.lanes[0] = {16, 10, 120, LaneMode::Random, 8, 80};
    Generator a, b, c;
    auto ea = noteOns(run(a, gp, 16.0, 512));
    auto eb = noteOns(run(b, gp, 16.0, 97));
    CHECK(ea.size() > 10);
    CHECK_EQ(ea.size(), eb.size());
    for (size_t i = 0; i < ea.size(); ++i) { CHECK_EQ(ea[i].d1, eb[i].d1); CHECK(std::abs(ea[i].ppq - eb[i].ppq) < 0.002); }
    gp.seed = 2;
    auto ec = noteOns(run(c, gp, 16.0, 512));
    bool differs = ec.size() != ea.size();
    for (size_t i = 0; !differs && i < ea.size(); ++i) differs = ea[i].d1 != ec[i].d1;
    CHECK(differs);   // another seed, another texture
}

TEST_CASE(gen_notes_stay_in_scale_and_range)
{
    auto gp = oneTrack();
    gp.root = 2; gp.scale = 3;   // D dorian
    auto& t = gp.tracks[0];
    t.pulses = 16;
    for (int m = 1; m < int(PitchMode::Count); ++m) {
        t.pitchMode = PitchMode(m); t.low = 45; t.high = 69; t.length = 5; t.mutate = 30;
        Generator g;
        for (const auto& e : noteOns(run(g, gp, 16.0))) {
            CHECK(e.d1 >= 45 && e.d1 <= 69);
            CHECK((kScales[3].mask >> ((e.d1 - 2 + 120) % 12)) & 1);
        }
    }
}

TEST_CASE(gen_loop_repeats_and_evolve_changes)
{
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.pulses = 9; t.pitchMode = PitchMode::Turing; t.low = 40; t.high = 76; t.mutate = 50; t.prob = 60;
    gp.loop = 1;   // 1 bar
    Generator g;
    auto ons = noteOns(run(g, gp, 16.0));
    std::map<int, std::vector<int>> bars;
    for (const auto& e : ons) bars[int(e.ppq / 4.0)].push_back(e.d1);
    CHECK(!bars[0].empty());
    for (int b = 1; b < 4; ++b) CHECK(bars[b] == bars[0]);   // LOOP: the bar comes back exactly
    gp.evolve = 60;
    Generator h;
    auto ons2 = noteOns(run(h, gp, 32.0));
    std::map<int, std::vector<int>> bars2;
    for (const auto& e : ons2) bars2[int(e.ppq / 4.0)].push_back(e.d1);
    bool changed = false;
    for (int b = 1; b < 8; ++b) changed |= bars2[b] != bars2[0];
    CHECK(changed);   // EVOLVE: the loop mutates as it comes around
}

TEST_CASE(gen_ratchets_and_locks)
{
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.ratchetChance = 100; t.ratchetMax = 3; t.ratchetShape = Shape::Flat;
    t.lanes[0] = {18, 20, 20, LaneMode::Random, 8, 100};   // FILT knob 3 locked to 20 on every trig
    Generator g;
    auto ev = run(g, gp, 4.0);
    auto ons = noteOns(ev);
    CHECK(ons.size() >= 8 && ons.size() <= 12);   // 4 trigs x 2..3 repeats
    int sets = 0, clears = 0;
    for (const auto& e : ev) if (e.sysex) { if (e.idx >= 0) { ++sets; CHECK_EQ(e.idx, 18); CHECK_EQ(e.value, 20); } else ++clears; }
    CHECK_EQ(sets, 4); CHECK_EQ(clears, 4);
    // the lock comes before its note
    for (size_t i = 0; i < ev.size(); ++i) if (!ev[i].sysex && (ev[i].status & 0xF0) == 0x90 && std::abs(ev[i].ppq - std::floor(ev[i].ppq)) < 1e-6) {
        bool lockBefore = false;
        for (size_t j = 0; j < i; ++j) lockBefore |= ev[j].sysex && ev[j].idx == 18 && std::abs(ev[j].ppq - ev[i].ppq) < 1e-6;
        CHECK(lockBefore);
    }
}

TEST_CASE(gen_conditions)
{
    auto gp = oneTrack();
    gp.tracks[0].cond = Cond::P1_2;   // first of every two pattern cycles
    Generator g;
    auto ons = noteOns(run(g, gp, 16.0));
    CHECK_EQ(int(ons.size()), 8);
    for (const auto& e : ons) CHECK_EQ(int((e.ppq + 1e-3) / 4.0) % 2, 0);
    // NEI: track 2 plays only after track 1 played
    auto gp2 = oneTrack();
    gp2.tracks[0].prob = 50;
    gp2.tracks[1] = gp2.tracks[0]; gp2.tracks[1].prob = 100; gp2.tracks[1].cond = Cond::Nei; gp2.tracks[1].low = 50;
    Generator h;
    auto ev = run(h, gp2, 16.0);
    auto a = noteOns(ev, 0), b = noteOns(ev, 1);
    CHECK_EQ(a.size(), b.size());
}

TEST_CASE(gen_stop_and_jump_release_notes)
{
    auto gp = oneTrack();
    gp.tracks[0].gate = 100;
    Generator g;
    run(g, gp, 1.01);
    Transport tr; tr.playing = false; tr.numSamples = 512; tr.sampleRate = 48000;
    g.process(tr, gp);
    bool allOff = false;
    for (int i = 0; i < g.numEvents(); ++i) allOff |= g.event(i).data[0] == 0xB0 && g.event(i).data[1] == 123;
    CHECK(allOff);
    // a jump (DAW loop) starts cleanly at the new position: the step on it plays
    Generator h;
    run(h, gp, 2.0);
    auto ons = noteOns(run(h, gp, 1.0, 512, 8.0));
    CHECK(!ons.empty());
    CHECK(std::abs(ons[0].ppq - 8.0) < 0.001);
}

TEST_CASE(gen_polymetric_rates)
{
    auto gp = oneTrack();
    gp.tracks[0].rate = 3;   // 1/20: quintuplet sixteenths, 20 steps per bar
    gp.tracks[0].steps = 20; gp.tracks[0].pulses = 20;
    Generator g;
    CHECK_EQ(int(noteOns(run(g, gp, 4.0)).size()), 20);
}

TEST_CASE(gen_loop_repeats_odd_lengths)
{
    // a 13-step track at 1/16 inside a 1-bar loop restarts with the loop: every bar is the same
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.steps = 13; t.pulses = 5; t.pitchMode = PitchMode::Random; t.low = 40; t.high = 70; t.prob = 70;
    gp.loop = 1;
    Generator g;
    std::map<int, std::vector<std::pair<int, int>>> bars;
    for (const auto& e : noteOns(run(g, gp, 12.0)))
        bars[int((e.ppq + 1e-3) / 4.0)].push_back({int(std::lround(std::fmod(e.ppq, 4.0) * 1000.0)), e.d1});
    CHECK(!bars[0].empty());
    CHECK(bars[1] == bars[0]);
    CHECK(bars[2] == bars[0]);
}

TEST_CASE(gen_grid_mode_plays_written_notes_and_lengths)
{
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.mode = TrackMode::Grid;
    t.steps = 8;
    t.grid[0] = gridCell(true, 60, 4);    // C4, one step
    t.grid[2] = gridCell(true, 63, 8);    // D#4, two steps
    t.grid[5] = gridCell(true, 67, 2);    // G4, half a step
    t.grid[6] = gridCell(false, 70, 4);   // no trig: silent
    Generator g;
    const auto ev = run(g, gp, 4.0);   // two cycles of 8 sixteenths
    const auto ons = noteOns(ev);
    CHECK_EQ(int(ons.size()), 6);
    const int expect[3] = {60, 63, 67};
    const double at[3] = {0.0, 0.5, 1.25};
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(ons[size_t(i)].d1, expect[i % 3]);
        CHECK(std::abs(ons[size_t(i)].ppq - (at[i % 3] + (i / 3) * 2.0)) < 1e-3);
    }
    // note offs at the written lengths: 1/4, 1/2 and 1/8 of a quarter after their note ons
    std::vector<double> offs;
    for (const auto& e : ev) if (!e.sysex && (e.status & 0xF0) == 0x80) offs.push_back(e.ppq);
    CHECK(offs.size() >= 3);
    CHECK(std::abs(offs[0] - 0.25) < 2e-3);
    CHECK(std::abs(offs[1] - 1.0) < 2e-3);
    CHECK(std::abs(offs[2] - 1.375) < 2e-3);
}

TEST_CASE(gen_grid_mode_keeps_chance_and_transpose)
{
    auto gp = oneTrack();
    auto& t = gp.tracks[0];
    t.mode = TrackMode::Grid;
    t.steps = 16;
    for (int i = 0; i < 16; ++i) t.grid[size_t(i)] = gridCell(true, 48 + i, 2);
    t.prob = 50;
    Generator g;
    g.setTranspose(12);
    const auto ons = noteOns(run(g, gp, 16.0));
    CHECK(ons.size() > 16 && ons.size() < 48);   // about half of 64
    for (const auto& e : ons) CHECK(e.d1 >= 60 && e.d1 < 76);   // transposed up an octave
}
