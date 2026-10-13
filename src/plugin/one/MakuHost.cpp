#include "MakuHost.h"
#if MNM_HAS_MAKU
#include "MakuData.h"
#endif

namespace mnm::plugin::one::maku {

const Verb kVerbs[kNumVerbs] = {
    {"SINK", "Sink", BFx, "SINK: TIENI PREMUTO. LA CASSA SPARISCE, RIVERBERO E DELAY SI APRONO, IL SUONO AFFONDA; LASCIA E TORNA PIANO"},
    {"CASCADE", "Cascade", BScl, "CASCADE: TIENI PREMUTO. L'ARPEGGIO SUONA OGNI STEP, CON OTTAVE E TERZE SOPRA: UNA PIOGGIA DI NOTE"},
    {"SWELL", "Swell", BEnv, "SWELL: TIENI PREMUTO. DRONE E SHIMMER RIATTACCANO L'ACCORDO, ATTACCHI E CODE SI ALLUNGANO, L'ACCORDO RESTA"},
    {"WOBBLE", "Wobble", BLfo, "WOBBLE: TIENI PREMUTO. FILTRI E CHORUS ONDEGGIANO VELOCI, L'INTONAZIONE OSCILLA"},
    {"LIFT", "Lift", BEdit, "LIFT: TIENI PREMUTO. TUTTI I TONE SALGONO AL MASSIMO, PIU' DRIVE, IL PASSA-ALTO SALE: IN AVANTI E LUMINOSO"},
    {"SCRAMBLE", "Scramble", BGlo, "SCRAMBLE: TIENI PREMUTO. ARPEGGIO E CASSA SI ROMPONO: SALTI, RIPETIZIONI, ROLLATE, COLPI CHE SALTANO"},
    {"DRIFT", "Drift", BPlay, "DRIFT: UN TOCCO ACCENDE / SPEGNE. I 12 MACRO SI MUOVONO DA SOLI A PICCOLI PASSI E OGNI TANTO CAMBIA UN TIMBRO"},
    {"NEW WORLD", "New World", BHome, "NEW WORLD: UN MONDO NUOVO (TIMBRI, TONALITA', MACRO). CON DEI LIKE, SPESSO NE RIPRENDE UNO E LO VARIA"},
    {"LIKE", "Like", BRec, "LIKE: UN TOCCO RICORDA IL MONDO ATTUALE (FINO A 16): I PROSSIMI NEW WORLD CRESCONO SUI TUOI GUSTI. 2 S = CANCELLA"},
};

const char* const kMakuScaleNames[kNumMakuScales] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                                     "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
const uint16_t kMakuScaleMasks[kNumMakuScales] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x295, 0x4A9, 0x9AD,
                                                  0x5AB, 0xAD5, 0x56B, 0xAAD, 0x4E9, 0x555, 0x6DB, 0xB6D};

namespace {
int bits(unsigned v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }
}
#define MK_POP(x) bits(x)

int makuScaleOf(int seqScale)
{
    const uint16_t m = gen::kScales[size_t(juce::jlimit(0, gen::kNumScales - 1, seqScale))].mask;
    int best = 2, score = -100;
    for (int i = 1; i < kNumMakuScales; ++i) {   // (CHR would leave MAKU without a key)
        const int s = 2 * MK_POP(unsigned(m & kMakuScaleMasks[i])) - MK_POP(unsigned(m ^ kMakuScaleMasks[i]));
        if (s > score) { score = s; best = i; }
    }
    return best;
}

int seqScaleOf(int makuScale)
{
    const uint16_t m = kMakuScaleMasks[juce::jlimit(0, kNumMakuScales - 1, makuScale)];
    int best = 2, score = -100;
    for (int i = 0; i < gen::kNumScales; ++i) {
        const int s = 2 * MK_POP(unsigned(m & gen::kScales[size_t(i)].mask)) - MK_POP(unsigned(m ^ gen::kScales[size_t(i)].mask));
        if (s > score) { score = s; best = i; }
    }
    return best;
}

void addLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    auto g = std::make_unique<juce::AudioProcessorParameterGroup>("maku", "MAKU", " ");
    g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kOnId, 1}, "MAKU ON", true));
    g->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kLevelId, 1}, "MAKU LEVEL", juce::NormalisableRange<float>(-60.0f, 24.0f, 0.5f), 9.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return v <= -60.0f ? juce::String("OFF") : (v > 0 ? "+" : "") + juce::String(v, 1) + " DB"; })));
    g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{kClockId, 1}, "MAKU CLOCK", juce::StringArray{"HOST", "FREE"}, 0));
    g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{kKeyId, 1}, "MAKU KEY", juce::StringArray{"SEQ > MAKU", "MAKU > SEQ", "OFF"}, 0));
    layout.add(std::move(g));
}

// ---------------------------------------------------------------------------
// MakuLib

struct MakuLib::Fns {
    void (*nor_erase)() = nullptr;
    void (*set_seed)(uint32_t) = nullptr;
    void (*boot)() = nullptr;
    void (*master)(uint32_t) = nullptr;
    void (*render)(uint32_t) = nullptr;
    float* (*out_l)() = nullptr;
    float* (*out_r)() = nullptr;
    void (*buttons)(uint32_t) = nullptr;
    void (*keys)(uint32_t) = nullptr;
    uint32_t (*lit_buttons)() = nullptr;
    uint32_t (*breath_buttons)() = nullptr;
    uint32_t (*lit_keys)() = nullptr;
    uint16_t* (*screen)() = nullptr;
    uint32_t (*screen_draws)() = nullptr;
    uint32_t (*playing)() = nullptr;
    uint32_t (*api_version)() = nullptr;
    void (*clock_mode)(uint32_t) = nullptr;
    uint32_t (*realtime)(uint32_t) = nullptr;
    uint32_t (*key_get)() = nullptr;
    void (*key_set)(uint32_t, uint32_t) = nullptr;
    void (*fifth)(int32_t) = nullptr;
    const char* (*part_name)(uint32_t) = nullptr;
    const char* (*macro_name)(uint32_t, uint32_t) = nullptr;
    const char* (*voice_name)(uint32_t) = nullptr;
    uint32_t (*focus_get)() = nullptr;
    void (*focus)(uint32_t) = nullptr;
    uint32_t (*level_get)(uint32_t) = nullptr;
    void (*level_set)(uint32_t, uint32_t) = nullptr;
    uint32_t (*macro_get)(uint32_t, uint32_t) = nullptr;
    void (*macro_set)(uint32_t, uint32_t, uint32_t) = nullptr;
    uint32_t (*bpm)() = nullptr;
    uint32_t (*drift_on)() = nullptr;
    uint32_t (*verbs_held)() = nullptr;
    uint32_t (*world_number)() = nullptr;
    uint32_t (*seed)() = nullptr;
    void (*new_world)() = nullptr;
    void (*like)() = nullptr;
    void (*likes_clear)() = nullptr;
    uint32_t (*likes)() = nullptr;
    uint32_t (*liked)() = nullptr;
    uint32_t (*likes_pack)(uint8_t*, uint32_t) = nullptr;
    uint32_t (*likes_unpack)(const uint8_t*, uint32_t) = nullptr;
    uint32_t (*world_pack)(uint8_t*) = nullptr;
    uint32_t (*world_apply)(const uint8_t*, uint32_t) = nullptr;
};

namespace {
#if JUCE_WINDOWS
constexpr const char* kLibExt = ".dll";
#elif JUCE_MAC
constexpr const char* kLibExt = ".dylib";
#else
constexpr const char* kLibExt = ".so";
#endif

// copies left by a host that crashed: anything of ours older than a day
void sweepStaleCopies(const juce::File& dir)
{
    const auto old = juce::Time::getCurrentTime() - juce::RelativeTime::days(1.0);
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "mnm-maku-*"))
        if (f.getLastModificationTime() < old) f.deleteFile();
}
}

MakuLib::MakuLib() : m_fn(std::make_unique<Fns>())
{
#if MNM_HAS_MAKU
    int size = 0;
    const char* data = MakuData::getNamedResource(MakuData::namedResourceList[0], size);
    if (data == nullptr || size <= 0) { m_error = "MAKU: library missing from the build"; return; }
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Monomodule");
    dir.createDirectory();
    sweepStaleCopies(dir);
    m_file = dir.getNonexistentChildFile("mnm-maku-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()), kLibExt, false);
    if (!m_file.replaceWithData(data, size_t(size))) { m_error = "MAKU: cannot write " + m_file.getFullPathName(); return; }
    m_lib = std::make_unique<juce::DynamicLibrary>();
    if (!m_lib->open(m_file.getFullPathName())) { m_error = "MAKU: cannot load " + m_file.getFullPathName(); m_lib.reset(); m_file.deleteFile(); return; }
    bool all = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(m_lib->getFunction(name));
        all = all && fn != nullptr;
    };
    auto& f = *m_fn;
    get(f.nor_erase, "web_nor_erase"); get(f.set_seed, "web_set_seed"); get(f.boot, "web_boot"); get(f.master, "web_master");
    get(f.render, "mk_render"); get(f.out_l, "web_out_l"); get(f.out_r, "web_out_r"); get(f.buttons, "web_buttons");
    get(f.keys, "web_keys"); get(f.lit_buttons, "web_lit_buttons"); get(f.breath_buttons, "web_breath_buttons");
    get(f.lit_keys, "web_lit_keys"); get(f.screen, "web_screen"); get(f.screen_draws, "web_screen_draws"); get(f.playing, "web_playing");
    get(f.api_version, "mk_api_version"); get(f.clock_mode, "mk_clock_mode"); get(f.realtime, "mk_realtime");
    get(f.key_get, "mk_key_get"); get(f.key_set, "mk_key_set"); get(f.fifth, "mk_fifth"); get(f.part_name, "mk_part_name");
    get(f.macro_name, "mk_macro_name"); get(f.voice_name, "mk_voice_name"); get(f.focus_get, "mk_focus_get"); get(f.focus, "mk_focus");
    get(f.level_get, "mk_level_get"); get(f.level_set, "mk_level_set"); get(f.macro_get, "mk_macro_get"); get(f.macro_set, "mk_macro_set");
    get(f.bpm, "mk_bpm"); get(f.drift_on, "mk_drift_on"); get(f.verbs_held, "mk_verbs_held"); get(f.world_number, "mk_world_number");
    get(f.seed, "mk_seed"); get(f.new_world, "mk_new_world"); get(f.like, "mk_like"); get(f.likes_clear, "mk_likes_clear"); get(f.likes, "mk_likes"); get(f.liked, "mk_liked");
    get(f.likes_pack, "mk_likes_pack"); get(f.likes_unpack, "mk_likes_unpack"); get(f.world_pack, "mk_world_pack");
    get(f.world_apply, "mk_world_apply");
    if (!all || f.api_version() != 1) { m_error = "MAKU: library does not match this plugin"; m_lib.reset(); m_file.deleteFile(); return; }
    m_ok = true;
#else
    m_error = "MAKU: not in this build";
#endif
}

MakuLib::~MakuLib()
{
    m_lib.reset();   // closed before its file can go
    if (m_file != juce::File()) m_file.deleteFile();
}

void MakuLib::boot(uint32_t seed)
{
    if (!m_ok) return;
    m_fn->nor_erase();
    m_fn->set_seed(seed);
    m_fn->boot();
    m_fn->master(1023);   // the device's MASTER pot all the way: the plugin's MAKU LEVEL is the volume
}

void MakuLib::render(int n, const float*& left, const float*& right)
{
    m_fn->render(uint32_t(juce::jlimit(0, 1024, n)));
    left = m_fn->out_l();
    right = m_fn->out_r();
}

void MakuLib::realtime(int status) { if (m_ok) m_fn->realtime(uint32_t(status)); }
void MakuLib::clockMode(bool external) { if (m_ok) m_fn->clock_mode(external ? 1u : 0u); }
int MakuLib::keyGet() const { return m_ok ? int(m_fn->key_get()) : 0; }
void MakuLib::keySet(int root, int scale) { if (m_ok) m_fn->key_set(uint32_t(root), uint32_t(scale)); }
void MakuLib::fifth(int dir) { if (m_ok) m_fn->fifth(dir); }
void MakuLib::buttons(uint32_t mask) { if (m_ok) m_fn->buttons(mask); }
void MakuLib::keys(uint32_t mask) { if (m_ok) m_fn->keys(mask); }
int MakuLib::focus() const { return m_ok ? int(m_fn->focus_get()) : 0; }
void MakuLib::setFocus(int part) { if (m_ok) m_fn->focus(uint32_t(part)); }
int MakuLib::level(int part) const { return m_ok ? int(m_fn->level_get(uint32_t(part))) : 0; }
void MakuLib::setLevel(int part, int v) { if (m_ok) m_fn->level_set(uint32_t(part), uint32_t(juce::jlimit(0, 127, v))); }
int MakuLib::macro(int part, int j) const { return m_ok ? int(m_fn->macro_get(uint32_t(part), uint32_t(j))) : 0; }
void MakuLib::setMacro(int part, int j, int v) { if (m_ok) m_fn->macro_set(uint32_t(part), uint32_t(j), uint32_t(juce::jlimit(0, 127, v))); }
const char* MakuLib::partName(int part) const { return m_ok ? m_fn->part_name(uint32_t(part)) : ""; }
const char* MakuLib::macroName(int part, int j) const { return m_ok ? m_fn->macro_name(uint32_t(part), uint32_t(j)) : ""; }
const char* MakuLib::voiceName(int part) const { return m_ok ? m_fn->voice_name(uint32_t(part)) : ""; }
int MakuLib::bpm() const { return m_ok ? int(m_fn->bpm()) : 0; }
bool MakuLib::drift() const { return m_ok && m_fn->drift_on() != 0; }
uint32_t MakuLib::verbsHeld() const { return m_ok ? m_fn->verbs_held() : 0u; }
uint32_t MakuLib::litButtons() const { return m_ok ? m_fn->lit_buttons() : 0u; }
uint32_t MakuLib::breathButtons() const { return m_ok ? m_fn->breath_buttons() : 0u; }
uint32_t MakuLib::litKeys() const { return m_ok ? m_fn->lit_keys() : 0u; }
int MakuLib::worldNumber() const { return m_ok ? int(m_fn->world_number()) : 0; }
uint32_t MakuLib::seed() const { return m_ok ? m_fn->seed() : 0u; }
bool MakuLib::playing() const { return m_ok && m_fn->playing() != 0; }
void MakuLib::newWorld() { if (m_ok) m_fn->new_world(); }
void MakuLib::like() { if (m_ok) m_fn->like(); }
void MakuLib::clearLikes() { if (m_ok) m_fn->likes_clear(); }
int MakuLib::likes() const { return m_ok ? int(m_fn->likes()) : 0; }
bool MakuLib::liked() const { return m_ok && m_fn->liked() != 0; }
const uint16_t* MakuLib::screen() const { return m_ok ? m_fn->screen() : nullptr; }
uint32_t MakuLib::screenDraws() const { return m_ok ? m_fn->screen_draws() : 0u; }

juce::MemoryBlock MakuLib::packLikes() const
{
    if (!m_ok) return {};
    juce::MemoryBlock b(m_fn->likes_pack(nullptr, 0));
    m_fn->likes_pack(static_cast<uint8_t*>(b.getData()), uint32_t(b.getSize()));
    return b;
}

void MakuLib::unpackLikes(const juce::MemoryBlock& b)
{
    if (m_ok && b.getSize() > 0) m_fn->likes_unpack(static_cast<const uint8_t*>(b.getData()), uint32_t(b.getSize()));
}

juce::MemoryBlock MakuLib::packWorld() const
{
    if (!m_ok) return {};
    uint8_t w[kWorldBytes];
    if (m_fn->world_pack(w) == 0) return {};
    return juce::MemoryBlock(w, sizeof w);
}

bool MakuLib::applyWorld(const juce::MemoryBlock& b)
{
    return m_ok && b.getSize() >= size_t(kWorldBytes) && m_fn->world_apply(static_cast<const uint8_t*>(b.getData()), uint32_t(b.getSize())) != 0;
}

} // namespace mnm::plugin::one::maku
