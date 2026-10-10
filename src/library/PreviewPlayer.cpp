#include "PreviewPlayer.h"
#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <utility>

namespace mnm::library {

namespace {
constexpr uint32_t kPrefillFrames = 44100 / 4;   // playback starts once this much (or everything) is rendered
constexpr int kMaxPad = 1 << 16;
const char* kNoOs = "Audio preview needs the Monomachine OS file (MENU)";
}

// ---------------------------------------------------------------------------
// PreviewRenderer

// The render thread gets a large stack: the emulator's JIT compiles and first-executes DSP code recursively, one
// level per block of a chain (hundreds deep through the kernel's longer stretches), which overflowed the 512 KB a
// secondary macOS thread gets by default (a tester's crash in 1.1.0: ___chkstk_darwin under JitBlockChain::create).
// MNM_RENDER_STACK_KB overrides the size (tests).
static size_t renderStackBytes()
{
    if (const char* e = std::getenv("MNM_RENDER_STACK_KB"); e && *e) return size_t(std::max(64, std::atoi(e))) * 1024;
    return size_t(16) << 20;
}

PreviewRenderer::PreviewRenderer() : juce::Thread("Monomachine preview render", renderStackBytes()) { startThread(juce::Thread::Priority::normal); }

PreviewRenderer::~PreviewRenderer()
{
    m_cancel.store(true);
    signalThreadShouldExit();
    m_jobEvent.signal();
    stopThread(5000);
}

void PreviewRenderer::setFirmwarePath(const juce::String& path)
{
    const juce::ScopedLock sl(m_jobLock);
    if (path == m_fwPath && !m_fwDirty) return;
    m_fwPath = path;
    m_fwDirty = true;
}

bool PreviewRenderer::hasFirmwarePath() const { const juce::ScopedLock sl(m_jobLock); return m_fwPath.isNotEmpty(); }

juce::String PreviewRenderer::status() const { const juce::ScopedLock sl(m_statusLock); return m_status; }

void PreviewRenderer::setStatus(const juce::String& s)
{
    { const juce::ScopedLock sl(m_statusLock); if (m_status == s) return; m_status = s; }
    if (onChange) onChange();
}

void PreviewRenderer::trimCache()
{
    size_t total = 0;
    for (const auto& [k, a] : m_cache) total += a->bytes();
    for (auto it = m_lru.begin(); it != m_lru.end() && total > m_cacheLimit;) {
        auto c = m_cache.find(*it);
        if (c == m_cache.end()) { it = m_lru.erase(it); continue; }
        if (c->second == m_keep || !c->second->done.load()) { ++it; continue; }   // never the one playing or rendering
        total -= c->second->bytes();
        m_cache.erase(c);
        it = m_lru.erase(it);
    }
}

std::shared_ptr<PreviewAudio> PreviewRenderer::request(const juce::String& key, double bpm, const std::function<preview::PreviewSpec()>& build)
{
    if (!hasFirmwarePath()) { setStatus(kNoOs); return nullptr; }
    const juce::String ck = key + "@" + juce::String(bpm, 2);
    if (auto it = m_cache.find(ck); it != m_cache.end() && !it->second->failed.load()) {
        m_lru.remove(ck); m_lru.push_back(ck);
        return it->second;
    }
    auto job = std::make_unique<Job>();
    job->key = ck; job->spec = build(); job->bpm = bpm;
    auto audio = std::make_shared<PreviewAudio>();
    audio->frames = ((job->spec.frames + 15) / 16) * 16;
    if (job->spec.loopFrames > 0 && job->spec.loops > 0 && uint32_t(job->spec.loops) * job->spec.loopFrames <= audio->frames) {
        audio->loopEnd = uint32_t(job->spec.loops) * job->spec.loopFrames;
        audio->loopStart = audio->loopEnd - job->spec.loopFrames;
    }
    audio->stems = job->spec.stems;
    audio->mixL.assign(audio->frames, 0.f); audio->mixR.assign(audio->frames, 0.f);
    if (audio->stems) for (int t = 0; t < 6; ++t) { audio->stemL[size_t(t)].assign(audio->frames, 0.f); audio->stemR[size_t(t)].assign(audio->frames, 0.f); }
    job->audio = audio;
    m_cache[ck] = audio;
    m_lru.remove(ck); m_lru.push_back(ck);
    trimCache();
    {
        const juce::ScopedLock sl(m_jobLock);
        if (m_current && !m_current->done.load()) {   // supersede the render in progress
            m_cancel.store(true);
            for (auto it = m_cache.begin(); it != m_cache.end();) it = it->second == m_current ? m_cache.erase(it) : std::next(it);
        }
        m_pending = std::move(job);
    }
    m_jobEvent.signal();
    setStatus({});
    return audio;
}

void PreviewRenderer::run()
{
    while (!threadShouldExit()) {
        std::unique_ptr<Job> job;
        {
            const juce::ScopedLock sl(m_jobLock);
            job = std::move(m_pending);
            if (job) { m_current = job->audio; m_cancel.store(false); }
        }
        if (!job) { m_jobEvent.wait(-1); continue; }
        m_rendering.store(true);
        renderJob(*job);
        m_rendering.store(false);
        { const juce::ScopedLock sl(m_jobLock); if (m_current == job->audio) m_current.reset(); }
        if (onChange) onChange();
    }
}

void PreviewRenderer::renderJob(Job& job)
{
    auto& a = *job.audio;
    auto fail = [&](const juce::String& why, bool report = true) { a.error = why.toStdString(); a.failed.store(true); a.done.store(true); if (report) setStatus(why); };
    juce::String path;
    bool dirty;
    { const juce::ScopedLock sl(m_jobLock); path = m_fwPath; dirty = m_fwDirty; m_fwDirty = false; }
    if (dirty || !m_firmware) {
        m_renderer.reset(); m_firmware.reset();
        if (path.isEmpty()) { fail(kNoOs); return; }
        try {
            m_firmware = std::make_unique<fw::Firmware>(fw::loadFirmware(path.toStdString()));
            m_renderer = std::make_unique<preview::KitRenderer>(*m_firmware);
        } catch (const std::exception& e) { fail("OS file: " + juce::String(e.what())); return; }
    }
    if (m_cancel.load() || threadShouldExit()) { fail("cancelled", false); return; }
    // loadKit resets the DSPs, which throws when a DSP cannot boot (no JIT memory: "DSP JIT failed ..."); on this
    // thread that must become a status, not a crash
    try { m_renderer->loadKit(job.spec.kit, job.bpm); }
    catch (const std::exception& e) { fail("Preview render failed: " + juce::String(e.what())); return; }
    const bool ok = m_renderer->render(job.spec.events, a.frames, [&](uint32_t f0, const preview::RenderBlock& b) {
        constexpr int N = preview::RenderBlock::kFrames;
        if (f0 + N > a.frames) return;
        std::copy(b.mixL.begin(), b.mixL.end(), a.mixL.begin() + f0);
        std::copy(b.mixR.begin(), b.mixR.end(), a.mixR.begin() + f0);
        if (a.stems)
            for (int t = 0; t < 6; ++t) {
                std::copy(b.stemL[size_t(t)].begin(), b.stemL[size_t(t)].end(), a.stemL[size_t(t)].begin() + f0);
                std::copy(b.stemR[size_t(t)].begin(), b.stemR[size_t(t)].end(), a.stemR[size_t(t)].begin() + f0);
            }
        a.ready.store(f0 + N);
    }, &m_cancel);
    if (!ok) { if (m_cancel.load()) fail("cancelled", false); else fail("Preview render failed: " + juce::String(m_renderer->error())); return; }
    a.done.store(true);
}

// ---------------------------------------------------------------------------
// PreviewVoice

PreviewVoice::PreviewVoice() { m_tmpL.resize(kMaxPad); m_tmpR.resize(kMaxPad); }

void PreviewVoice::prepare(double sampleRate)
{
    if (std::abs(sampleRate - preview::kSampleRate) < 0.5 || std::abs(sampleRate - m_kernelRate) < 0.5) return;
    m_kernel.design(preview::kSampleRate, sampleRate);
    m_taps.assign(size_t(m_kernel.numTaps()), 0.f);
    m_kernelRate = sampleRate;
}

void PreviewVoice::start(std::shared_ptr<PreviewAudio> audio, int stem)
{
    const juce::SpinLock::ScopedLockType sl(m_lock);
    m_playing = std::move(audio);
    m_stem = m_playing && m_playing->stems ? stem : -1;
    m_pos = 0; m_frac = 0.0; m_started = false;
    m_playPos.store(0); m_finished.store(false); m_loop.store(false);
}

void PreviewVoice::stop() { const juce::SpinLock::ScopedLockType sl(m_lock); m_playing.reset(); m_playPos.store(0); }

bool PreviewVoice::active() const { const juce::SpinLock::ScopedLockType sl(m_lock); return m_playing != nullptr; }

double PreviewVoice::progress() const
{
    const juce::SpinLock::ScopedLockType sl(m_lock);
    return m_playing && m_playing->frames ? double(m_playPos.load()) / double(m_playing->frames) : 0.0;
}

bool PreviewVoice::process(float* outL, float* outR, int n, double sampleRate, bool replace)
{
    if (n <= 0 || n > kMaxPad || !outL) return false;
    std::shared_ptr<PreviewAudio> a;
    int stem;
    {
        const juce::SpinLock::ScopedTryLockType sl(m_lock);
        if (!sl.isLocked() || !m_playing) return false;
        a = m_playing; stem = m_stem;
    }
    if (a->failed.load()) { if (!m_finished.exchange(true)) {} return false; }
    const uint32_t ready = a->ready.load();
    if (!m_started) {
        if (ready < std::min(a->frames, kPrefillFrames) && !a->done.load()) return false;   // prefill
        m_started = true;
    }
    // the loop region, once it is rendered: the pattern's last pass, else the whole buffer
    const uint32_t loopStart = a->loopEnd > a->loopStart ? a->loopStart : 0, loopEnd = a->loopEnd > a->loopStart ? a->loopEnd : a->frames;
    const bool loop = m_loop.load() && loopEnd > loopStart && (ready >= loopEnd || a->done.load());
    if (loop && m_pos >= loopEnd) m_pos = loopStart;   // (switched on during the tail: back into the pattern at once)
    if (m_pos >= a->frames) { m_finished.store(true); return false; }
    // the position `n` frames on, wrapping inside the loop region
    auto advance = [&](uint32_t pos, uint32_t by) { return pos + by < loopEnd ? pos + by : loopStart + (pos + by - loopEnd) % (loopEnd - loopStart); };
    const float* srcL = stem >= 0 ? a->stemL[size_t(stem)].data() : a->mixL.data();
    const float* srcR = stem >= 0 ? a->stemR[size_t(stem)].data() : a->mixR.data();
    float* tl = m_tmpL.data();
    float* tr = m_tmpR.data();
    std::fill_n(tl, n, 0.f); std::fill_n(tr, n, 0.f);
    const uint32_t avail = ready - std::min(ready, m_pos);
    if (std::abs(sampleRate - preview::kSampleRate) < 0.5) {   // the render's own rate: the frames themselves
        if (loop) {   // read modulo the length: the block that crosses the end continues from the start
            for (int i = 0; i < n; ++i, m_pos = advance(m_pos, 1)) { tl[i] = srcL[m_pos]; tr[i] = srcR[m_pos]; }
        } else {
            const uint32_t want = uint32_t(n);
            if (avail < want && !a->done.load()) return false;   // the render is behind: a silent block, then continue
            const uint32_t take = std::min(want, avail);
            if (take) { std::copy_n(srcL + m_pos, take, tl); std::copy_n(srcR + m_pos, take, tr); }
            m_pos += take;
        }
    } else {
        // another rate: output sample k is the band-limited render at m_pos + m_frac, then the position moves on by
        // 44100 / rate frames. The taps reach H frames either side: ahead they wrap inside the loop region, past the
        // render's end and before its start they read silence.
        if (!m_kernel.ready() || std::abs(sampleRate - m_kernelRate) > 0.5) return false;   // not prepared for this rate
        const double step = preview::kSampleRate / sampleRate;
        const int half = m_kernel.halfTaps(), taps = m_kernel.numTaps();
        if (!loop && avail < uint32_t(std::ceil(n * step)) + uint32_t(half) + 2 && !a->done.load()) return false;   // the render is behind
        const uint32_t frames = a->frames;
        for (int k = 0; k < n; ++k) {
            if (!loop && m_pos >= frames) break;
            m_kernel.taps(m_frac, m_taps.data());
            double l = 0.0, r = 0.0;
            for (int j = 0; j < taps; ++j) {
                const int off = j - half + 1;
                uint32_t idx;
                if (off < 0) { if (m_pos < uint32_t(-off)) continue; idx = m_pos - uint32_t(-off); }
                else idx = loop ? advance(m_pos, uint32_t(off)) : m_pos + uint32_t(off);
                if (idx >= frames) continue;
                l += double(m_taps[size_t(j)]) * srcL[idx];
                r += double(m_taps[size_t(j)]) * srcR[idx];
            }
            tl[k] = float(l); tr[k] = float(r);
            m_frac += step;
            const auto whole = uint32_t(m_frac);
            m_frac -= whole;
            m_pos = loop ? advance(m_pos, whole) : std::min(frames, m_pos + whole);
        }
    }
    if (replace) { std::copy_n(tl, n, outL); if (outR) std::copy_n(tr, n, outR); }
    else { for (int i = 0; i < n; ++i) outL[i] += tl[i]; if (outR) for (int i = 0; i < n; ++i) outR[i] += tr[i]; }
    m_playPos.store(m_pos);
    if (m_pos >= a->frames && !loop) m_finished.store(true);
    return true;
}

// ---------------------------------------------------------------------------
// PreviewPlayer

#if JUCE_MAC
// JUCE reports devices arriving and leaving, but not the system switching its output to another one. Core Audio
// calls the listener on a thread of its own, and may still be inside it while the last player goes, so the listener
// touches no player: the players are told on the message thread.
struct PreviewPlayer::SystemOutputWatch {
    static std::vector<PreviewPlayer*>& players() { static std::vector<PreviewPlayer*> p; return p; }   // message thread
    static const AudioObjectPropertyAddress& address()
    {
        static const AudioObjectPropertyAddress a{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        return a;
    }
    static OSStatus changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void*)
    {
        juce::MessageManager::callAsync([] {
            for (auto* p : players()) {
                // the new output may be a device that has only just arrived: the list first, as JUCE does for arrivals
                if (auto* type = p->m_devices.getCurrentDeviceTypeObject()) type->scanForDevices();
                p->outputsChanged();
            }
        });
        return noErr;
    }
    static void add(PreviewPlayer* p)
    {
        if (players().empty()) AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address(), changed, nullptr);
        players().push_back(p);
    }
    static void remove(PreviewPlayer* p)
    {
        auto& v = players();
        v.erase(std::remove(v.begin(), v.end(), p), v.end());
        if (v.empty()) AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address(), changed, nullptr);
    }
};
#endif

PreviewPlayer::PreviewPlayer()
{
    m_renderer.onChange = [this] { triggerAsyncUpdate(); };
    m_devices.addAudioCallback(this);
    m_devices.addChangeListener(this);   // a device arrived or left (Windows: or the system switched its output)
#if JUCE_MAC
    SystemOutputWatch::add(this);
#endif
}

PreviewPlayer::~PreviewPlayer()
{
#if JUCE_MAC
    SystemOutputWatch::remove(this);
#endif
    m_devices.removeChangeListener(this);
    m_renderer.onChange = nullptr;
    cancelPendingUpdate();
    m_devices.removeAudioCallback(this);
    m_devices.closeAudioDevice();
}

juce::String PreviewPlayer::status() const { const auto s = m_renderer.status(); return s.isNotEmpty() ? s : m_deviceStatus; }

// The kind of device in use (Core Audio, Windows Audio, ALSA); the kinds are created and scanned at the first call.
juce::AudioIODeviceType* PreviewPlayer::outputType()
{
    m_devices.getAvailableDeviceTypes();
    return m_devices.getCurrentDeviceTypeObject();
}

juce::StringArray PreviewPlayer::outputDevices()
{
    auto* type = outputType();
    return type ? type->getDeviceNames(false) : juce::StringArray();
}

juce::String PreviewPlayer::systemOutput()
{
    auto* type = outputType();
    return type ? type->getDeviceNames(false)[type->getDefaultDeviceIndex(false)] : juce::String();
}

// The device the choice means now: the chosen one, or the system's output. A chosen device that is not there ends
// the choice.
juce::String PreviewPlayer::resolveOutput()
{
    if (m_outputChoice.isNotEmpty() && !outputDevices().contains(m_outputChoice)) {
        const auto gone = std::exchange(m_outputChoice, {});
        if (onOutputChoiceEnded) onOutputChoiceEnded(gone);
    }
    return m_outputChoice.isNotEmpty() ? m_outputChoice : systemOutput();
}

// Makes the device the choice means the open one (the open device is left alone when it is that one already).
// False, with a status, when no output opens: the next preview tries again.
bool PreviewPlayer::openOutput()
{
    auto open = [this](const juce::String& name) -> juce::String {
        if (name.isEmpty()) return "none found";
        if (m_devices.getCurrentAudioDevice() != nullptr && m_devices.getAudioDeviceSetup().outputDeviceName == name) return {};
        juce::AudioDeviceManager::AudioDeviceSetup setup;   // rate and buffer size: the device's own
        setup.outputDeviceName = name;
        const auto err = m_devices.initialise(0, 2, nullptr, false, {}, &setup);
        return err.isEmpty() && m_devices.getCurrentAudioDevice() == nullptr ? "cannot open " + name : err;
    };
    auto err = open(resolveOutput());
    if (err.isNotEmpty() && m_outputChoice.isNotEmpty()) {   // the chosen device will not open: the system's output
        const auto failed = std::exchange(m_outputChoice, {});
        if (onOutputChoiceEnded) onOutputChoiceEnded(failed);
        err = open(resolveOutput());
    }
    m_deviceReady = err.isEmpty();
    m_deviceStatus = err.isEmpty() ? juce::String() : "No audio output device: " + err;
    return m_deviceReady;
}

// A device arrived or left, the system switched its output, or the choice changed. An open output follows; a preview
// left without one ends. Before the first preview nothing is opened, a chosen device is only looked for.
void PreviewPlayer::outputsChanged()
{
    if (m_deviceReady) { if (!openOutput()) stop(); }
    else if (m_outputChoice.isNotEmpty()) resolveOutput();
    triggerAsyncUpdate();   // the status may have changed
}

void PreviewPlayer::setOutputChoice(const juce::String& device)
{
    m_outputChoice = device;
    outputsChanged();
}

void PreviewPlayer::play(const juce::String& key, const std::function<preview::PreviewSpec()>& build, int stem)
{
    // the output first: this is also where a switched system output is noticed on a platform that does not report it
    const bool os = m_renderer.hasFirmwarePath();
    if (os && !openOutput()) stop();   // no output: what was playing has none either
    if (!os || !m_deviceReady) { m_renderer.request(key, m_options.bpm, build); triggerAsyncUpdate(); return; }
    auto audio = m_renderer.request(key, m_options.bpm, build);
    if (!audio) { triggerAsyncUpdate(); return; }
    m_audio = audio;
    m_renderer.keep(audio);
    m_voice.start(audio, stem);
    m_playingKey = key;
    m_playingStem = audio->stems ? stem : -1;
    triggerAsyncUpdate();
}

void PreviewPlayer::stop()
{
    m_voice.stop();
    m_playingKey.clear(); m_playingStem = -1;
    triggerAsyncUpdate();
}

void PreviewPlayer::handleAsyncUpdate()
{
    if (m_voice.consumeFinished() || (isPlaying() && m_audio && m_audio->failed.load())) { m_voice.stop(); m_playingKey.clear(); m_playingStem = -1; }
    if (onChange) onChange();
}

void PreviewPlayer::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    m_deviceRate = device ? device->getCurrentSampleRate() : 44100.0;
    m_voice.prepare(m_deviceRate);
}

void PreviewPlayer::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
    if (numOut < 1 || n <= 0 || !out[0]) return;
    const bool was = m_voice.active();
    m_voice.process(out[0], numOut > 1 ? out[1] : nullptr, n, m_deviceRate, false);
    if (was && m_voice.progress() >= 1.0) triggerAsyncUpdate();
}

} // namespace mnm::library
