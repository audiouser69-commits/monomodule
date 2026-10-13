// MIX on the Euclid LCD: see MixPage in EuclidEditor.h.
#include "EuclidEditor.h"

namespace mnm::plugin::one {

namespace {
const spec::Font& tiny() { return spec::kFontTiny3x5; }
const spec::Font& small() { return spec::kFontSmall4x5; }
const spec::Font& bold() { return spec::kFontBold8; }
int toLcd(int px) { return px / kScale; }
juce::String fit(const spec::Font& f, juce::String s, int maxW)
{
    while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > maxW) s = s.dropLastCharacters(1);
    return s;
}
void outline(LcdCanvas& cv, int x, int y, int w, int h)
{
    cv.fillRect(x, y, w, 1); cv.fillRect(x, y + h - 1, w, 1); cv.fillRect(x, y, 1, h); cv.fillRect(x + w - 1, y, 1, h);
}

// LCD px
constexpr int kStrips = 7, kStripX = 4, kStripW = 50, kTopY = 17, kBarY = 38, kBarH = 92, kMuteY = 141, kMuteH = 10;
constexpr int kMeterDx = 4, kMeterW = 6, kFaderDx = 14, kFaderW = 28;
constexpr int kRouteX = 360, kRouteW = 96, kHelpY = 158;

float meterPos(float peak)   // -60 .. 0 dB as 0 .. 1
{
    if (peak <= 0.001f) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak) + 60.0f) / 60.0f);
}
}

MixPage::MixPage(MnmOneProcessor& p) : m_proc(p)
{
    struct F { SeqField* f; juce::String id; const char* label; const char* help; };
    for (const F& f : {F{&m_outputs, outputModeId(), "OUTPUTS", "OUTPUTS: STEREO MIX = TUTTE LE TRACCE E MAKU SULL'USCITA 1 (DEFAULT); PER TRACK = OGNI TRACCIA SULLA SUA USCITA, MAKU SULLA 7; BUSES = I BUS AB CD EF SULLE USCITE 1-3"},
                       F{&m_send, maku::kSendId, "SEND", "SEND: MAKU ENTRA ANCHE IN UN BUS DI MIX. UNA TRACCIA FX CON INGRESSO BUS AB/CD/EF (REVERB, CHORUS, RINGMOD...) LO ELABORA"},
                       F{&m_sendLevel, maku::kSendLevelId, "LVL", "SEND LEVEL: QUANTO MAKU VA NEL BUS"},
                       F{&m_duck, maku::kDuckId, "DUCK", "DUCK: LA CASSA DI MAKU ABBASSA LE SEI TRACCE, COME ABBASSA I SUOI DRONI (DA DENSITY 40 IN SU, PIU' FORTE PIU' E' DENSA)"},
                       F{&m_side, maku::kSideId, "SIDE", "SIDECHAIN: QUESTA TRACCIA ABBASSA MAKU A OGNI COLPO (PER ESEMPIO LA CASSA EUCLIDEA SU T1)"},
                       F{&m_sideAmt, maku::kSideAmtId, "DEPTH", "SIDECHAIN DEPTH: QUANTO SCENDE MAKU"},
                       F{&m_drift, maku::kDriftId, "DRIFT", "DRIFT > SEQ: CON DRIFT ACCESO IN MAKU ANCHE I PATTERN EUCLIDEI VAGANO PIANO (PULSES, ROTATE, CHANCE), UN POSTO NUOVO OGNI 4 BATTUTE"}}) {
        f.f->bind(p.apvts, f.id);
        f.f->label = f.label;
        const juce::String help = f.help;
        f.f->helpFn = [help] { return help; };
        f.f->addMouseListener(this, false);
        addAndMakeVisible(*f.f);
    }
    m_outputs.textFn = [](int v) { return juce::String(v == 0 ? "PER TRACK" : v == 1 ? "BUSES AB CD EF" : "STEREO MIX"); };
    m_proc.makuUi(m_ui);
}

void MixPage::resized()
{
    m_outputs.setBounds(66 * kScale, 3 * kScale, 110 * kScale, 9 * kScale);
    int y = kTopY + 12;
    for (auto* f : {&m_send, &m_sendLevel, &m_duck, &m_side, &m_sideAmt, &m_drift}) {
        f->setBounds((kRouteX + 2) * kScale, y * kScale, (kRouteW - 4) * kScale, 9 * kScale);
        y += f == &m_sendLevel || f == &m_sideAmt ? 15 : 11;
    }
}

void MixPage::tick()
{
    m_proc.makuUi(m_ui);
    for (int i = 0; i < kStrips; ++i) {
        const float pk = i < 6 ? m_proc.trackPeak(i) : m_proc.makuPeak();
        m_meter[size_t(i)] = juce::jmax(meterPos(pk), m_meter[size_t(i)] - 0.04f);
    }
    if (isVisible()) repaint();
}

juce::RangedAudioParameter* MixPage::fader(int strip) const
{
    return m_proc.apvts.getParameter(strip < 6 ? levelId(strip) : juce::String(maku::kLevelId));
}

juce::RangedAudioParameter* MixPage::muteParam(int strip) const
{
    return m_proc.apvts.getParameter(strip < 6 ? muteId(strip) : juce::String(maku::kOnId));
}

int MixPage::hit(juce::Point<int> local, bool& mute) const
{
    const int x = toLcd(local.x), y = toLcd(local.y);
    mute = false;
    if (x < kStripX || x >= kStripX + kStrips * kStripW || y < kTopY || y >= kMuteY + kMuteH) return -1;
    if (y >= kMuteY) mute = true;
    return (x - kStripX) / kStripW;
}

juce::String MixPage::helpOf(int strip, bool mute) const
{
    if (strip < 0) return {};
    if (strip == 6) return mute ? "MAKU ON / OFF" : "MAKU LEVEL: TRASCINA SU/GIU' O ROTELLA, DOPPIO CLICK = +9 DB. IL METER E' L'USCITA DI MAKU";
    return "T" + juce::String(strip + 1) + (mute ? juce::String(" MUTE") : juce::String(" LEVEL: TRASCINA SU/GIU' O ROTELLA, DOPPIO CLICK = 100. IL METER E' L'USCITA DELLA TRACCIA"));
}

void MixPage::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    bool mute = false;
    const int s = hit(e.getPosition(), mute);
    if (s < 0) return;
    if (mute) {
        if (auto* p = muteParam(s)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        return;
    }
    if (auto* p = fader(s)) { m_drag = s; m_dragStart = p->getValue(); p->beginChangeGesture(); }
}

void MixPage::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent != this || m_drag < 0) return;
    if (auto* p = fader(m_drag)) {
        const float px = float(kBarH * kScale) * (e.mods.isShiftDown() ? 4.0f : 1.0f);
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, m_dragStart - float(e.getDistanceFromDragStartY()) / px));
    }
}

void MixPage::mouseUp(const juce::MouseEvent&)
{
    if (m_drag >= 0)
        if (auto* p = fader(m_drag)) p->endChangeGesture();
    m_drag = -1;
}

void MixPage::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    bool mute = false;
    const int s = hit(e.getPosition(), mute);
    if (s >= 0 && !mute)
        if (auto* p = fader(s)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture(); }
}

void MixPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.eventComponent != this) return;
    bool mute = false;
    const int s = hit(e.getPosition(), mute);
    if (s < 0 || mute) return;
    if (auto* p = fader(s)) {
        m_wheel += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
        const int steps = int(m_wheel / 0.05f);
        if (steps == 0) return;
        m_wheel -= float(steps) * 0.05f;
        const auto& r = p->getNormalisableRange();
        const float v = juce::jlimit(r.start, r.end, p->convertFrom0to1(p->getValue()) + float(steps) * (s < 6 ? 1.0f : 0.5f));
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
}

void MixPage::mouseMove(const juce::MouseEvent& e)
{
    if (e.eventComponent == this) {
        bool mute = false;
        const int s = hit(e.getPosition(), mute);
        const int h = s < 0 ? -1 : s * 2 + (mute ? 1 : 0);
        if (h != m_hover) { m_hover = h; repaint(); }
        setHelp(helpOf(s, mute));
        return;
    }
    if (auto* f = dynamic_cast<SeqField*>(e.eventComponent); f && f->helpFn) setHelp(f->helpFn());
}

void MixPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    cv.fillRect(0, 0, 62, 14);
    cv.text(bold(), "MIX", 4, 3, false);
    const juce::String st = "TRACKS + MAKU";
    cv.text(small(), st.toRawUTF8(), kLcdW - 4 - LcdCanvas::textWidth(small(), st.toRawUTF8()), 5);
    cv.dotsH(0, kLcdW - 1, 13);

    for (int s = 0; s < kStrips; ++s) {
        const int x = kStripX + s * kStripW;
        const bool maku = s == 6;
        juce::String name = maku ? juce::String("MAKU") : "T" + juce::String(s + 1), sub;
        if (!maku) {
            const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(s))->load())));
            sub = spec::kMachines[slot].name;
        } else {
            sub = m_proc.makuAvailable() ? "AMBIENT" : "N/A";
        }
        cv.text(bold(), name.toRawUTF8(), x + 2, kTopY);
        cv.text(tiny(), fit(tiny(), sub.toUpperCase(), kStripW - 6).toRawUTF8(), x + 2, kTopY + 11);
        // meter
        outline(cv, x + kMeterDx, kBarY, kMeterW, kBarH);
        const int mh = int(std::lround((kBarH - 2) * m_meter[size_t(s)]));
        if (mh > 0) cv.fillRect(x + kMeterDx + 1, kBarY + kBarH - 1 - mh, kMeterW - 2, mh);
        // fader
        auto* p = fader(s);
        const float v = p ? p->getValue() : 0.0f;
        cv.dotsV(x + kFaderDx + kFaderW / 2, kBarY, kBarY + kBarH - 1);
        const int ky = kBarY + int(std::lround((1.0f - v) * float(kBarH - 6)));
        cv.fillRect(x + kFaderDx, ky, kFaderW, 6);
        cv.fillRect(x + kFaderDx + 2, ky + 2, kFaderW - 4, 2, false);
        if (m_hover == s * 2 || m_drag == s) outline(cv, x + kFaderDx - 2, kBarY - 2, kFaderW + 4, kBarH + 4);
        const juce::String val = p ? (maku ? p->getCurrentValueAsText().replace(" DB", "") : juce::String(int(std::lround(p->convertFrom0to1(v))))) : juce::String();
        cv.textCentred(small(), fit(small(), val, kStripW - 4).toRawUTF8(), x, kStripW - 2, kBarY + kBarH + 3);
        // MUTE (tracks) / ON (MAKU)
        auto* m = muteParam(s);
        const bool on = m && m->getValue() >= 0.5f;
        const bool lit = maku ? !on : on;   // inverted: muted, or MAKU off
        if (lit) cv.fillRect(x + 2, kMuteY, kStripW - 6, kMuteH);
        else outline(cv, x + 2, kMuteY, kStripW - 6, kMuteH);
        cv.textCentred(tiny(), maku ? (on ? "ON" : "OFF") : "MUTE", x + 2, kStripW - 6, kMuteY + 3, !lit);
        if (m_hover == s * 2 + 1) cv.dotsH(x + 2, x + kStripW - 5, kMuteY + kMuteH + 1);
        if (s < kStrips - 1) cv.dotsV(x + kStripW - 2, kTopY, kMuteY + kMuteH - 1);
    }

    // the routing between the engines, with the ducking it does now
    cv.fillRect(kRouteX, kTopY - 1, kRouteW, 10);
    cv.text(bold(), "ROUTING", kRouteX + 3, kTopY, false);
    auto gauge = [&](const char* label, float a, int y) {
        cv.text(tiny(), label, kRouteX + 2, y);
        outline(cv, kRouteX + 30, y, kRouteW - 32, 5);
        const int w = int(std::lround(float(kRouteW - 34) * juce::jlimit(0.0f, 1.0f, a)));
        if (w > 0) cv.fillRect(kRouteX + 31, y + 1, w, 3);
    };
    gauge("DUCK", m_ui.duck, kTopY + 98);
    gauge("SIDE", m_ui.side, kTopY + 106);
    cv.text(tiny(), m_ui.drift ? "DRIFT ON IN MAKU" : "DRIFT OFF IN MAKU", kRouteX + 2, kTopY + 116);

    // help
    const juce::String help = m_help.isNotEmpty() ? m_help
        : juce::String("TRASCINA I FADER, CLICK SU MUTE. DI DEFAULT TUTTO ESCE DALL'USCITA 1 (STEREO MIX): PER SEPARARE LE USCITE SCEGLI OUTPUTS = PER TRACK. "
                       "SEND MANDA MAKU A UNA TRACCIA FX; DUCK E SIDE FANNO RESPIRARE I DUE MOTORI UNO SULL'ALTRO");
    {
        juce::StringArray words = juce::StringArray::fromTokens(help, " ", "");
        juce::String line;
        int y = kHelpY;
        for (const auto& w : words) {
            const juce::String t = line.isEmpty() ? w : line + " " + w;
            if (LcdCanvas::textWidth(tiny(), t.toRawUTF8()) > kLcdW - 8 && line.isNotEmpty()) {
                cv.text(tiny(), line.toRawUTF8(), 4, y);
                y += 7;
                line = w;
                if (y > kLcdH - 6) break;
            } else line = t;
        }
        if (y <= kLcdH - 6 && line.isNotEmpty()) cv.text(tiny(), line.toRawUTF8(), 4, y);
    }
    cv.dotsH(0, kLcdW - 1, kHelpY - 4);
    cv.draw(g, 0, 0);
}

} // namespace mnm::plugin::one
