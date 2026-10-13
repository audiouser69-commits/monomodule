// Monomodule Bench: how much of an Android phone the emulated Monomachine tracks and MAKU take.
//   nativeLoad   the OS file (copied into the app's cache by the Java side)
//   nativeBench  offline: 1..6 tracks (the Euclid kit's machines, notes retriggered) and MAKU, each rendered for some
//                seconds of audio as fast as the phone can; the time it took as a share of real time
//   nativeStart  live: N tracks and MAKU through AAudio, as the app would play them; nativeStatus reports the
//                callback's load and the underruns (crackles) counted by the system
#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "MonoVoice.h"
#include "firmware/Firmware.h"

using namespace mnm;
using Clock = std::chrono::steady_clock;

namespace {

std::unique_ptr<fw::Firmware> g_fw;
std::mutex g_lock;   // (the UI thread's calls; the audio callback never takes it)

constexpr host::Machine kKit[6] = {host::Machine::BBOX, host::Machine::BBOX, host::Machine::FM_DYN,
                                   host::Machine::SID, host::Machine::SAW, host::Machine::ENS};
constexpr const char* kKitNames[6] = {"DPRO BBOX", "DPRO BBOX", "FM+ DYN", "SID 6581", "SWAVE SAW", "SWAVE ENS"};
constexpr int kNotes[6] = {48, 52, 60, 36, 57, 64};

std::unique_ptr<MonoVoice> makeVoice(int t)
{
    auto v = std::make_unique<MonoVoice>(*g_fw);
    v->host().setMachine(kKit[t % 6]);
    v->warmUp(64);
    v->host().noteOn(kNotes[t % 6]);
    return v;
}

// MAKU, through its library (a private copy is not needed here: one instance)
struct Maku {
    void* h = nullptr;
    void (*erase)() = nullptr; void (*seed)(uint32_t) = nullptr; void (*boot)() = nullptr; void (*master)(uint32_t) = nullptr;
    void (*render)(uint32_t) = nullptr; float* (*outL)() = nullptr; float* (*outR)() = nullptr; void (*clock)(uint32_t) = nullptr;
    bool open()
    {
        if (h) return true;
        h = dlopen("libmaku.so", RTLD_NOW | RTLD_LOCAL);
        if (!h) return false;
        auto get = [&](auto& f, const char* n) { f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(dlsym(h, n)); return f != nullptr; };
        const bool ok = get(erase, "web_nor_erase") && get(seed, "web_set_seed") && get(boot, "web_boot") && get(master, "web_master")
                        && get(render, "mk_render") && get(outL, "web_out_l") && get(outR, "web_out_r") && get(clock, "mk_clock_mode");
        if (!ok) { dlclose(h); h = nullptr; return false; }
        erase(); seed(1977); boot(); master(1023); clock(0);   // its own clock: it plays by itself
        return true;
    }
} g_maku;

double seconds(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char s[256];
    std::snprintf(s, sizeof s, f, a, b, c);
    return s;
}

// ---- live
struct Live {
    AAudioStream* stream = nullptr;
    std::vector<std::unique_ptr<MonoVoice>> voices;
    bool maku = false;
    std::vector<float> l, r;
    int64_t frames = 0;
    std::atomic<double> loadAvg{0.0}, loadMax{0.0};
    double rate = 44100.0;
} g_live;

aaudio_data_callback_result_t onAudio(AAudioStream*, void*, void* data, int32_t n)
{
    auto& L = g_live;
    const auto t0 = Clock::now();
    float* out = static_cast<float*>(data);
    if (int(L.l.size()) < n) { std::fill_n(out, size_t(n) * 2, 0.0f); return AAUDIO_CALLBACK_RESULT_CONTINUE; }
    std::fill_n(L.l.data(), n, 0.0f);
    std::fill_n(L.r.data(), n, 0.0f);
    float vl[1024], vr[1024];
    for (int pos = 0; pos < n;) {
        const int c = std::min(1024, n - pos);
        for (size_t t = 0; t < L.voices.size(); ++t) {
            L.voices[t]->process(vl, vr, c);
            for (int i = 0; i < c; ++i) { L.l[size_t(pos + i)] += vl[i]; L.r[size_t(pos + i)] += vr[i]; }
        }
        if (L.maku) {
            g_maku.render(uint32_t(c));
            const float* ml = g_maku.outL();
            const float* mr = g_maku.outR();
            for (int i = 0; i < c; ++i) { L.l[size_t(pos + i)] += 3.0f * ml[i]; L.r[size_t(pos + i)] += 3.0f * mr[i]; }
        }
        pos += c;
    }
    // retrigger the notes every half second, as a sequence would
    const int64_t half = int64_t(L.rate / 2);
    if ((L.frames % half) + n >= half)
        for (size_t t = 0; t < L.voices.size(); ++t) L.voices[t]->host().noteOn(kNotes[t % 6]);
    L.frames += n;
    for (int i = 0; i < n; ++i) { out[2 * i] = std::tanh(L.l[size_t(i)] * 0.35f); out[2 * i + 1] = std::tanh(L.r[size_t(i)] * 0.35f); }
    const double load = seconds(t0, Clock::now()) / (double(n) / L.rate);
    L.loadAvg.store(L.loadAvg.load() * 0.95 + load * 0.05);
    if (load > L.loadMax.load()) L.loadMax.store(load);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void stopLive()
{
    if (g_live.stream) {
        AAudioStream_requestStop(g_live.stream);
        AAudioStream_close(g_live.stream);
        g_live.stream = nullptr;
    }
    g_live.voices.clear();
}

std::string jstr(JNIEnv* env, jstring s)
{
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL Java_world_shnolk_mnm_bench_MainActivity_nativeLoad(JNIEnv* env, jobject, jstring path)
{
    std::lock_guard<std::mutex> lk(g_lock);
    std::string msg;
    try {
        g_fw = std::make_unique<fw::Firmware>(fw::loadFirmware(jstr(env, path)));
        auto v = makeVoice(0);
        msg = "OS" + g_fw->version + " caricato. Motore DSP: " + (v->engine().usingJit() ? "JIT (ARM64)" : "interprete (lento)") + ".";
    } catch (const std::exception& e) {
        g_fw.reset();
        msg = std::string("Errore: ") + e.what();
    }
    return env->NewStringUTF(msg.c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_world_shnolk_mnm_bench_MainActivity_nativeBench(JNIEnv* env, jobject, jint secs)
{
    std::lock_guard<std::mutex> lk(g_lock);
    std::string out;
    const double dur = std::max(1, int(secs));
    const int frames = int(44100 * dur);
    float l[256], r[256];
    // MAKU first (no OS file needed)
    if (g_maku.open()) {
        const auto t0 = Clock::now();
        for (int f = 0; f < frames; f += 256) g_maku.render(256);
        const double load = seconds(t0, Clock::now()) / dur;
        out += fmt("MAKU: %.1f%% di un core\n", load * 100.0);
    } else {
        out += "MAKU: libreria non caricata\n";
    }
    if (!g_fw) return env->NewStringUTF((out + "\nScegli il file OS per misurare le tracce Monomachine.").c_str());
    double one = 0.0;
    int fit = 0;
    for (int n = 1; n <= 6; ++n) {
        std::vector<std::unique_ptr<MonoVoice>> v;
        const auto s0 = Clock::now();
        for (int t = 0; t < n; ++t) v.push_back(makeVoice(t));
        const double setup = seconds(s0, Clock::now());
        const auto t0 = Clock::now();
        for (int f = 0; f < frames; f += 256) {
            for (auto& x : v) x->process(l, r, 256);
            if (f % 22050 < 256) for (int t = 0; t < n; ++t) v[size_t(t)]->host().noteOn(kNotes[t]);
        }
        const double load = seconds(t0, Clock::now()) / dur;
        if (n == 1) one = load;
        if (load < 0.7) fit = n;   // (a margin for the UI, the system and MAKU)
        out += fmt("%.0f tracce: %.1f%% di un core", double(n), load * 100.0) + fmt(" (avvio %.2f s)", setup) + "  + " + kKitNames[n - 1] + "\n";
    }
    out += fmt("\nUna traccia: %.1f%% di un core. ", one * 100.0);
    out += fit == 6 ? "Le sei tracce entrano in tempo reale con margine."
           : fit > 0 ? fmt("In tempo reale con margine: %.0f tracce su sei.", double(fit))
                     : "Nemmeno una traccia ha margine: servirebbe un telefono più veloce.";
    out += "\nProva ora ASCOLTA per sentire se gracchia.";
    return env->NewStringUTF(out.c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_world_shnolk_mnm_bench_MainActivity_nativeStart(JNIEnv* env, jobject, jint tracks, jboolean maku)
{
    std::lock_guard<std::mutex> lk(g_lock);
    stopLive();
    if (tracks > 0 && !g_fw) return env->NewStringUTF("Scegli prima il file OS (o 0 tracce per sentire solo MAKU).");
    if (maku && !g_maku.open()) return env->NewStringUTF("MAKU non disponibile.");
    for (int t = 0; t < tracks; ++t) g_live.voices.push_back(makeVoice(t));
    g_live.maku = maku;
    g_live.frames = 0;
    g_live.loadAvg = 0.0;
    g_live.loadMax = 0.0;
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return env->NewStringUTF("AAudio non disponibile.");
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 44100);   // the engine's own rate (the system resamples if it must)
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, onAudio, nullptr);
    g_live.l.assign(8192, 0.0f);
    g_live.r.assign(8192, 0.0f);
    const auto rc = AAudioStreamBuilder_openStream(b, &g_live.stream);
    AAudioStreamBuilder_delete(b);
    if (rc != AAUDIO_OK) { g_live.stream = nullptr; g_live.voices.clear(); return env->NewStringUTF("Impossibile aprire l'audio."); }
    g_live.rate = AAudioStream_getSampleRate(g_live.stream);
    AAudioStream_setBufferSizeInFrames(g_live.stream, AAudioStream_getFramesPerBurst(g_live.stream) * 2);
    AAudioStream_requestStart(g_live.stream);
    std::string m = "In ascolto: " + std::to_string(int(tracks)) + " tracce" + (maku ? " + MAKU" : "") + ", " +
                    std::to_string(int(g_live.rate)) + " Hz, blocchi di " + std::to_string(AAudioStream_getFramesPerBurst(g_live.stream)) + " campioni.";
    return env->NewStringUTF(m.c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_world_shnolk_mnm_bench_MainActivity_nativeStatus(JNIEnv* env, jobject)
{
    if (!g_live.stream) return env->NewStringUTF("");
    const int xruns = AAudioStream_getXRunCount(g_live.stream);
    std::string s = fmt("Carico audio: medio %.0f%%, massimo %.0f%%. ", g_live.loadAvg.load() * 100.0, g_live.loadMax.load() * 100.0) +
                    "Interruzioni (gracchi): " + std::to_string(xruns) + ".";
    if (g_live.loadAvg.load() > 0.85) s += " Troppo: riduci le tracce.";
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_world_shnolk_mnm_bench_MainActivity_nativeStop(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    stopLive();
}
