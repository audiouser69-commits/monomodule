// Monomodule One / Six / FX processor. One emulated track per hardware track (each with its own DSP engine,
// all sharing one loaded OS), behind a UI built from the hardware's own LCD glyphs and fonts.
//   One (numTracks = 1): one track, every machine; synths play MIDI notes on any channel, FX machines
//       process the side-chain input.
//   Six (numTracks = 6): multitimbral, MIDI channels 1..6 address tracks 1..6 (the hardware's individual
//       track channels), one stereo output bus per track. FX machines take the previous track's output
//       (NEIBOR) or the side-chain input (INP AB), as KIT > ROUTING offers on the hardware.
//   FX (Variant::Fx): One as an audio effect. One track that offers only the FX machines and processes the
//       plugin's main input; no MIDI. It runs as a continuous effect: the knobs that act only on a trig or a note
//       (host::kTrigOnlyWords) are held open in the engine and hidden in the editor, but stay parameters with their
//       stored values. Everything else (pages, LFOs, library, state, the parameter list) is One's.
// Every knob is stored raw 0..127 exactly as the hardware kit byte; only the display differs.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "MonoVoice.h"
#include "dsp/Resampler.h"
#include "OneParams.h"
#include "library/MnmDump.h"
#include "PreviewPlayer.h"

namespace mnm::plugin::one {

class MnmOneProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater {
public:
    explicit MnmOneProcessor(Variant variant);
    explicit MnmOneProcessor(int numTracks = 1) : MnmOneProcessor(numTracks > 1 ? Variant::Six : Variant::One) { jassert(numTracks == 1 || numTracks == 6); }
    ~MnmOneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Host samples the sample-rate conversion delays every output by (0 at 44.1 kHz): the input converter's look-ahead
    // plus the output converter's. Part of the reported latency (getLatencySamples), which for Monomodule FX also
    // holds the emulated FX path's own delay.
    int conversionLatency() const { return m_inLag + m_outLag; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return m_variant == Variant::Fx ? "Monomodule FX" : m_variant == Variant::Euclid ? "Monomodule Euclid" : m_numTracks > 1 ? "Monomodule Six" : "Monomodule One"; }
    bool acceptsMidi() const override { return m_variant != Variant::Fx; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int numTracks() const { return m_numTracks; }
    Variant variant() const { return m_variant; }
    bool isEffect() const { return m_variant == Variant::Fx; }
    bool isEuclid() const { return m_variant == Variant::Euclid; }

    // Monomodule Euclid: the generative sequencer (null on the other plugins). It plays the six tracks with notes and
    // p-locks (a lock holds a knob at a value until the track's next trig, like the hardware's parameter locks),
    // following the host transport (or its own clock: SEQ RUN WITHOUT HOST, and in the standalone app).
    // Notes on MIDI channel 16 transpose every melodic track (latched; C4 = no transposition).
    mnm::gen::Generator* generator() const { return m_gen.get(); }
    uint32_t stepMask(int t) const { return m_stepMask[size_t(t)].load(); }   // bit n = step n muted by hand
    void setStepMask(int t, uint32_t mask) { m_stepMask[size_t(t)].store(mask); }

    // GRID mode: the steps written by hand (gen::gridCell packing). Not parameters; saved with the session.
    uint32_t gridCell(int t, int step) const { return m_grid[size_t(t)][size_t(step & 31)].load(); }
    void setGridCell(int t, int step, uint32_t cell) { m_grid[size_t(t)][size_t(step & 31)].store(cell); }

    // The panel's keyboard and pads: notes played into a track from the editor (message thread).
    void auditionNote(int t, int note, bool on);

    // REC: notes arriving from the host (any channel but 16) are played on, and written into, the record track.
    // The editor reads them back with popRecorded(): playing = the transport ran, step = the step that was playing.
    struct RecordedNote { int note = 60; int step = -1; bool playing = false; };
    void setRecord(bool armed, int track) { m_recTrack.store(track); m_recArmed.store(armed); }
    bool recordArmed() const { return m_recArmed.load(); }
    bool popRecorded(RecordedNote& out);
    // MAKU (Euclid): the ambient generator beside the six tracks (MakuHost.h). The editor reads makuUi() and sends
    // makuCommand(); both are safe from the message thread. Its audio has a stereo bus of its own after the tracks
    // ("MAKU"), folded into the main output when the host leaves that bus off.
    bool makuAvailable() const { return m_maku && m_maku->available(); }
    juce::String makuError() const { return m_maku ? m_maku->error() : juce::String(); }
    void makuUi(maku::MakuUi& out) const;   // false available when there is no MAKU
    void makuCommand(const maku::MakuCommand& c);
    float makuPeak() const { return m_makuPeak.load(); }

    int lockValue(int t, int target) const { return m_tracks[size_t(t)]->lock[size_t(target)].load(); }   // -1 = no lock
    float trackPeak(int t) const { return m_tracks[size_t(t)]->peak.load(); }   // linear, pre-mute, decays between UI polls
    float outputPeak() const { return trackPeak(0); }
    float hostBpm() const { return m_hostBpm.load(); }   // last tempo reported by the host transport (120 without one)
    host::Machine currentMachine(int t = 0) const { return m_tracks[size_t(t)]->currentMachine; }

    // Machine-change side effects (message thread): each machine's SYN values live in plugin state, so a
    // machine change saves the outgoing machine's eight values and restores the incoming machine's (or its
    // hardware defaults on first use); switching between a synth and an FX machine loads the AMP page defaults
    // the assign routine loads (FX: envelope held open); hosts are told the parameter names changed.
    // Runs from an async update after any machine parameter change; dev tools without a message loop call it.
    void syncMachineSideEffects();

    // Library transfer (message thread): applies a kit track (machine, SYN/AMP/FILT/EFX/LFO pages, level,
    // key tracking, and on Six the OUT BUS / FX INPUT routing) to track t, or a whole kit to a Six. The
    // ASSIGN matrix and the KIT > TRIG settings are not applied (not modelled yet).
    // soundOnly = a preset load: the track keeps its level and routing (they belong to the kit).
    bool applyKitTrack(int t, const mnm::dump::Kit& kit, int kitTrack, juce::String& error, bool soundOnly = false);
    bool applyKit(const mnm::dump::Kit& kit, juce::String& error);   // locked tracks keep their sound

    // What the library knows about the plugin's sounds (message thread, kept in the plugin state): the preset each
    // track was loaded from and the kit (Six), so the editor can name them, step through the library from them and
    // offer "save as a new version of ...". "Modified" = the sound no longer matches what was loaded. The bytes the
    // plugin does not model (ASSIGN matrix, MIDI page, KIT > TRIG) are carried along, so a sound that goes through
    // the plugin and back into a project loses nothing.
    struct LoadedRef { juce::String id, name; bool valid() const { return id.isNotEmpty() || name.isNotEmpty(); } };
    bool loadPreset(int t, const mnm::dump::Kit& kit, int kitTrack, const juce::String& id, const juce::String& name, juce::String& error);
    bool loadKit(const mnm::dump::Kit& kit, const juce::String& id, const juce::String& name, juce::String& error);
    LoadedRef loadedPreset(int t) const { return m_tracks[size_t(t)]->loaded; }
    LoadedRef loadedKit() const { return m_loadedKit; }
    bool presetModified(int t) const;
    bool kitModified() const;
    void markPresetSaved(int t, const juce::String& id, const juce::String& name);   // after a save: the current sound is that item
    void markKitSaved(const juce::String& id, const juce::String& name);
    bool trackLocked(int t) const { return m_tracks[size_t(t)]->locked; }
    void setTrackLocked(int t, bool locked) { m_tracks[size_t(t)]->locked = locked; }
    mnm::dump::Kit currentKit() const;   // the plugin's sound as a kit (One: track 1, the rest GND... see .cpp)

    // Library previews, mixed into the main output (a plugin has no device of its own). The renderer and its
    // emulated DSPs exist from the first preview on.
    void previewPlay(const juce::String& key, const std::function<mnm::preview::PreviewSpec()>& build);
    void previewStop();
    juce::String previewKey() const { return m_previewKey; }
    void previewSetLoop(bool on) { m_previewVoice.setLoop(on && m_previewKey.isNotEmpty()); }   // off again on stop or another preview
    bool previewLoop() const { return m_previewKey.isNotEmpty() && m_previewVoice.loop(); }
    juce::String previewStatus() const { return m_previewRenderer ? m_previewRenderer->status() : juce::String(); }
    bool previewPoll();   // editor timer: true when the playing state changed

    // Firmware management (message thread), same contract as the other plugins.
    juce::String firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const juce::String& path, bool persist = true);
    void refreshSharedOsPath();
    void clearFirmware();
    void initPreset();
    juce::String statusText() const;
    bool engineReady() const { return m_engineReady.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Track {
        int index = 0;
        std::unique_ptr<MonoVoice> voice;
        std::vector<int> heldNotes;   // note stack (last-note priority)
        host::Machine currentMachine = host::Machine::SAW;   // what the engine plays
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* syn[8] = {};
        std::atomic<float>* pages[4][8] = {};   // [SYN] unused
        std::atomic<float>* lfo[spec::kNumLfos][8] = {};
        std::atomic<float>* level = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* input = nullptr;    // host::FxInput (Six only)
        std::atomic<float>* outBus[3] = {};     // OUT BUS AB/CD/EF (Six only)
        std::atomic<float>* lpKeyTrack = nullptr;
        std::atomic<float>* hpKeyTrack = nullptr;
        int lastFrames = 0;                     // engine frames rendered in the last chunk (engL/R)
        std::vector<float> engL, engR;     // 44.1 kHz render (also the NEIBOR feed for the next track)
        std::vector<float> inL, inR;       // silence for an FX machine on track 1 reading NEIBOR
        std::array<dsp::History, 2> hist;  // the render's history, for the output converter (host rate != 44.1 kHz)
        std::atomic<float> peak{0.0f};
        // Euclid p-locks (audio thread; read by the editor): page * 8 + knob -> raw value, -1 = the parameter's own
        std::array<std::atomic<int>, mnm::gen::kLockTargets> lock{};
        // Per-machine SYN memory (state, not parameters): the eight values of every machine this track
        // has visited, and the machine slot whose values the SYN parameters currently hold.
        std::array<std::array<uint8_t, 8>, spec::kNumMachines> shadow{};
        std::array<bool, spec::kNumMachines> visited{};
        int shadowSlot = 0;
        // library state (message thread)
        LoadedRef loaded;
        std::vector<uint8_t> loadedPrint;   // soundPrint() at load time
        mnm::dump::KitTrack carry;          // the loaded track: the source of the bytes that are not parameters
        bool locked = false;
    };
    std::vector<uint8_t> soundPrint(int t, bool withKitBytes) const;
    mnm::dump::KitTrack currentKitTrack(int t) const;
    juce::ValueTree libraryToTree() const;
    void libraryFromTree(const juce::ValueTree&);

    static BusesProperties makeBuses(Variant variant);
    void loadEngine();
    void applyParametersToHost(Track&);
    void handleMidi(Track&, const juce::MidiMessage&);
    void pushParamFromCC(Track&, host::Page page, int k, int value);
    void processChunk(juce::AudioBuffer<float>&, int offset, int n);
    void runGenerator(const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos, const juce::MidiBuffer& midi, int n);
    void scheduleMakuClock(const mnm::gen::Transport& tr, int n);
    void makuBlockStart(const juce::MidiBuffer& midi);   // the last block's waiting events first, then the DAW's MIDI for MAKU
    void pushMaku(int64_t frame, int kind, int a = 0, int b = 0, int c = 0);
    void makuApplyButtons();
    void makuSend(int nEngine);        // MAKU into a mix bus (before the tracks, so an FX track on that bus hears it)
    void makuSidechain(int nEngine);   // a track ducks MAKU (after the tracks)
    void renderMaku(int64_t firstFrame, int nEngine);
    void followMakuKey();   // message thread: MAKU > SEQ
    static bool isLockSysex(const juce::MidiMessage& m);
    void fillSideChain(int offset, int n, int64_t firstFrame, int nEngine);
    void renderTrack(Track&, int64_t firstFrame, int nEngine);
    void writeOutputs(juce::AudioBuffer<float>&, int offset, int n, int nEngine);
    int64_t engineFrameOf(int64_t hostSample) const { return m_needsResample ? m_engineAt.ceilAt(hostSample) : hostSample; }
    bool readsSideChain(const Track& tr) const;
    int latencyAt(double rate) const;
    int outBusMask(const Track& tr) const;
    host::FxInput fxInputOf(const Track& tr) const;
    // A KIT > ASSIGN > KEY flag as the engine gets it. Monomodule FX plays no notes, only the fixed note-60 trig that opens
    // its envelope, so tracking would park the filters at that note: the HPF alone took 29 dB off THRU at 30 Hz
    // (issue #5). The parameter keeps its stored value (presets carry it to and from the hardware).
    bool keyTracks(const std::atomic<float>* flag) const { return !isEffect() && flag->load() >= 0.5f; }
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override { syncMachineSideEffects(); }
    int machineSlotOf(const Track& tr) const { return juce::jlimit(0, spec::kNumMachines - 1, int(tr.machine->load())); }
    void migrateLegacyState(juce::ValueTree& state) const;
    juce::ValueTree shadowsToTree() const;
    void shadowsFromTree(const juce::ValueTree& shadows);

    const Variant m_variant;
    const int m_numTracks;
    juce::String m_firmwarePath;
    std::unique_ptr<fw::Firmware> m_firmware;   // shared by all track voices
    std::vector<std::unique_ptr<Track>> m_tracks;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};
    juce::String m_status;
    double m_hostRate = 44100.0;
    bool m_needsResample = false;
    std::atomic<float>* m_bpm = nullptr;
    std::atomic<float>* m_bpmSync = nullptr;
    std::atomic<float>* m_outputMode = nullptr;   // Six only
    std::array<std::vector<float>, 3> m_busL, m_busR;   // mix buses AB/CD/EF at 44.1 kHz, rebuilt every chunk in track order
    std::atomic<float> m_hostBpm{120.0f};
    bool m_wasSynced = true;
    std::vector<float> m_sideL, m_sideR;   // side-chain input captured before the buffer is cleared (host rate)
    int m_sideChannels = 0;

    // The clock (see Resampler.h). The engine runs at 44.1 kHz whatever the host's rate: host sample s is engine time
    // s * 44100 / rate, exactly. A host block is processed in chunks of at most m_maxChunk samples; each renders the
    // engine frames whose input has fully arrived, so the engine trails the host by the input converter's look-ahead
    // (m_inLag), and every output is m_outLag later still, so its converter only reads frames already rendered. MIDI
    // events wait in m_midi for their engine frame. At 44.1 kHz nothing is converted and nothing waits: the engine
    // frames are the host samples.
    int m_maxChunk = 512, m_maxEngine = 512;    // host samples / engine frames per chunk at most
    int m_inLag = 0, m_outLag = 0;              // host samples
    int64_t m_hostPos = 0, m_enginePos = 0;     // processed since prepareToPlay
    dsp::SincKernel m_toHost, m_toEngine;       // engine -> host rate, host -> engine rate
    dsp::RateMap m_engineAt, m_hostAt;          // engine position of a host sample, host position of an engine frame
    std::array<dsp::History, 2> m_sideHist;     // side-chain history (host rate)
    std::array<std::array<dsp::History, 2>, 3> m_busHist;   // mix-bus history (engine rate)
    std::vector<float> m_sideEngL, m_sideEngR;  // the side-chain at the engine rate, this chunk (FX machines on INP A/B/AB)
    std::vector<float> m_taps;                  // scratch: one sample's filter taps
    struct PendingMidi { int64_t frame; juce::MidiMessage msg; };
    std::vector<PendingMidi> m_midi;            // events by engine frame, oldest first (fixed capacity)
    static constexpr size_t kMaxPendingMidi = 4096;
    // Bypassed: the main input delayed by the reported latency (processBlockBypassed)
    juce::AudioBuffer<float> m_bypassDelay;
    int m_bypassPos = 0;
    LoadedRef m_loadedKit;
    std::vector<uint8_t> m_loadedKitPrint;
    mnm::dump::Kit m_kitCarry;
    std::unique_ptr<mnm::library::PreviewRenderer> m_previewRenderer;
    mnm::library::PreviewVoice m_previewVoice;
    std::shared_ptr<mnm::library::PreviewAudio> m_previewAudio;
    juce::String m_previewKey;

    // Monomodule Euclid
    std::unique_ptr<mnm::gen::Generator> m_gen;
    euclid::Bindings m_seqBind;
    mnm::gen::GlobalParams m_genParams;
    std::array<std::atomic<uint32_t>, mnm::gen::kTracks> m_stepMask{};
    double m_freePpq = 0.0;   // the sequencer's own clock (no host transport)
    euclid::Bindings::GridStore m_grid{};
    juce::MidiMessageCollector m_uiMidi;   // auditions from the panel
    std::atomic<bool> m_recArmed{false};
    std::atomic<int> m_recTrack{0};
    juce::AbstractFifo m_recFifo{64};
    std::array<RecordedNote, 64> m_recBuffer{};

    // MAKU (Euclid). The library is called from the audio thread only, or with m_engineLock held (state).
    std::unique_ptr<maku::MakuLib> m_maku;
    std::atomic<float>* m_mkOn = nullptr;
    std::atomic<float>* m_mkLevel = nullptr;
    std::atomic<float>* m_mkClock = nullptr;
    std::atomic<float>* m_mkKey = nullptr;
    std::atomic<float>* m_mkChord = nullptr;
    std::atomic<float>* m_mkVerbs = nullptr;
    std::atomic<float>* m_mkFeed = nullptr;
    std::atomic<float>* m_mkSend = nullptr;
    std::atomic<float>* m_mkSendLevel = nullptr;
    std::atomic<float>* m_mkDuck = nullptr;
    std::atomic<float>* m_mkSide = nullptr;
    std::atomic<float>* m_mkSideAmt = nullptr;
    std::atomic<float>* m_mkDriftAmt = nullptr;
    std::vector<float> m_mkDuckGain;            // the tracks' gain under MAKU's kick, this chunk (per frame)
    bool m_mkDuckActive = false, m_mkDriftOn = false;
    float m_mkDuckLast = 1.0f, m_mkSideEnv = 0.0f, m_mkSideGain = 1.0f, m_mkDuckShown = 0.0f;
    uint32_t m_mkPanelButtons = 0, m_mkMidiButtons = 0, m_mkSentButtons = 0;
    std::array<bool, 128> m_mkCcHigh{};         // the DAW's CC on channel 15 last above 63 (edges)
    uint16_t m_mkChordNow = 0;                  // the link, read after each render (audio thread)
    std::array<float, 6> m_mkVerbNow{};
    int m_mkTempoSent = 0;
    std::vector<float> m_makuL, m_makuR;        // this chunk at 44.1 kHz
    std::array<dsp::History, 2> m_makuHist;
    int m_makuFrames = 0;                       // rendered in the last chunk (0: silent)
    std::atomic<float> m_makuPeak{0.0f};
    float m_makuGain = 0.0f;                    // smoothed
    // what reaches MAKU at an engine frame: a clock byte, a note (a part, b note, c velocity | 0x100 learn), a note it
    // only learns (a), the DAW's verb keys (a mask), NEW WORLD, LIKE
    enum MakuKind : int { MkRealtime, MkNote, MkLearn, MkMidiButtons, MkNewWorld, MkLike };
    struct MakuEvent { int64_t frame; int kind, a, b, c; };
    std::array<MakuEvent, 256> m_mkEvents{};    // clock bytes by engine frame, this block
    int m_mkEventCount = 0, m_mkEventNext = 0;
    bool m_mkExtClock = false, m_mkRunning = false, m_mkWasOn = true;
    int64_t m_mkNextPulse = 0;                  // the 1/24 quarter pulse expected next (host ppq * 24)
    int m_mkLastSeqKey = -1;                    // SEQ > MAKU: the key last given to MAKU
    juce::AbstractFifo m_mkFifo{256};
    std::array<maku::MakuCommand, 256> m_mkCommands{};
    juce::CriticalSection m_mkCommandLock;      // (writers: the editor, the state)
    mutable juce::SpinLock m_mkUiLock;
    std::unique_ptr<maku::MakuUi> m_mkUi;       // the copy the editor reads
    uint32_t m_mkUiDraws = 0;
    int64_t m_mkUiAt = 0;
    std::atomic<int> m_mkKeyNow{-1};            // MAKU's key, root | scale << 8 (for MAKU > SEQ)
    struct KeyFollower : juce::Timer { MnmOneProcessor* p = nullptr; void timerCallback() override { p->followMakuKey(); } } m_mkFollower;
};

} // namespace mnm::plugin::one
