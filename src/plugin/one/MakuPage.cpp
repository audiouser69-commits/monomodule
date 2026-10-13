// MAKU on the Euclid LCD: see MakuPage in EuclidEditor.h.
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
constexpr int kScrX = 3, kScrY = 17, kScrW = 160;               // MAKU's screen, drawn 2 x 2 real px a dot
constexpr int kColX = 170, kColW = 288, kTitleY = 14, kRowY = 26, kRowH = 30;
constexpr int kNameW = 46, kLevelW = 64, kMacroW = 58;
constexpr int kButtonsY = 150, kHelpY = 170;

const char* const kPartRole[maku::kParts] = {"KICK", "DRONE", "SHIMMER", "ARP"};
const char* const kPartHelp[maku::kParts] = {
    "PULSE: LA CASSA. DENS = DA UN COLPO A BATTUTA AL QUATTRO QUARTI (POI HI-HAT). CON PULSE A FUOCO I TASTI BIANCHI SONO I 16 STEP",
    "FLOOR: IL DRONE, GLI ACCORDI LENTI DELLA SCALA. OPEN = QUANTE VOCI (SOTTO META': GLI ACCORDI SI ALLUNGANO)",
    "HAZE: LO SHIMMER GRANULARE IN ALTO. GRAIN = DENSITA' DEI GRANI (SOTTO META': SI DIRADA)",
    "VOICE: LA FRASE ARPEGGIATA. LOOSE = QUANTO SI SCIOGLIE E SALE. LE NOTE CHE SUONI ENTRANO NELLA FRASE"};
}

MakuPage::MakuPage(MnmOneProcessor& p) : m_proc(p)
{
    struct F { SeqField* f; const char* id; const char* label; const char* help; };
    for (const F& f : {F{&m_clock, maku::kClockId, "CLOCK", "CLOCK: HOST = MAKU SEGUE IL TEMPO E IL PLAY DELLA DAW (PARTE SULLA BATTUTA); FREE = IL SUO TEMPO, SEMPRE IN MOTO"},
                       F{&m_key, maku::kKeyId, "KEY", "KEY: SEQ > MAKU = MAKU SUONA NELLA ROOT/SCALE DEL SEQUENCER; MAKU > SEQ = IL SEQUENCER PRENDE LA TONALITA' DEL MONDO; OFF = OGNUNO LA SUA"},
                       F{&m_level, maku::kLevelId, "LEVEL", "LEVEL: VOLUME DI MAKU (SUL SUO BUS 'MAKU', O NEL MAIN SE LA DAW NON LO USA)"},
                       F{&m_on, maku::kOnId, "MAKU", "MAKU ON / OFF: SPENTO NON SUONA E NON CONSUMA CPU"}}) {
        f.f->bind(p.apvts, f.id);
        f.f->label = f.label;
        const juce::String help = f.help;
        f.f->helpFn = [help] { return help; };
        f.f->addMouseListener(this, false);
        addAndMakeVisible(*f.f);
    }
    m_newWorld.onClick = [this] { m_proc.makuCommand({maku::MakuCommand::NewWorld}); };
    m_like.onClick = [this] {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return;
        m_proc.makuCommand({maku::MakuCommand::Like});
    };
    m_like.addMouseListener(this, false);
    m_flat.onClick = [this] { fifth(m_proc, -1); };
    m_sharp.onClick = [this] { fifth(m_proc, 1); };
    for (auto* b : {&m_newWorld, &m_like, &m_flat, &m_sharp}) { b->addMouseListener(this, false); addAndMakeVisible(*b); }
    m_proc.makuUi(m_ui);
}

void MakuPage::fifth(MnmOneProcessor& p, int dir)
{
    // SEQ > MAKU: the sequencer's ROOT moves a fifth (MAKU follows as a change of key signature); otherwise MAKU's own
    if (int(std::lround(p.apvts.getRawParameterValue(maku::kKeyId)->load())) == int(maku::KeyLink::SeqToMaku)) {
        if (auto* r = p.apvts.getParameter(euclid::gid("root"))) {
            const int root = int(std::lround(r->convertFrom0to1(r->getValue())));
            r->beginChangeGesture();
            r->setValueNotifyingHost(r->convertTo0to1(float((root + (dir > 0 ? 7 : 5)) % 12)));
            r->endChangeGesture();
        }
    } else {
        p.makuCommand({maku::MakuCommand::Fifth, dir});
    }
}

void MakuPage::tick()
{
    m_proc.makuUi(m_ui);
    if (!isVisible()) return;   // (the panel still reads the state: the transport window)
    if (m_ui.screenSerial != m_screenSerial) {
        m_screenSerial = m_ui.screenSerial;
        juce::Image::BitmapData bd(m_screen, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < maku::kScreen; ++y)
            for (int x = 0; x < maku::kScreen; ++x) {
                const uint16_t v = m_ui.screen[size_t(y * maku::kScreen + x)];
                const uint16_t c = uint16_t((v >> 8) | (v << 8));   // the panel's byte order -> RGB565
                const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                bd.setPixelColour(x, y, juce::Colour(uint8_t((r * 527 + 23) >> 6), uint8_t((g * 259 + 33) >> 6), uint8_t((b * 527 + 23) >> 6)));
            }
    }
    repaint();
}

void MakuPage::resized()
{
    int x = 66;
    auto place = [&](juce::Component& c, int w) { c.setBounds(x * kScale, 3 * kScale, w * kScale, 9 * kScale); x += w + 4; };
    place(m_on, 34);
    place(m_clock, 46);
    place(m_key, 64);
    place(m_level, 56);
    const int bw = (kColW - 3 * 4) / 4;
    int bx = kColX;
    for (auto* b : {&m_newWorld, &m_like, &m_flat, &m_sharp}) { b->setBounds(bx * kScale, kButtonsY * kScale, bw * kScale, 14 * kScale); bx += bw + 4; }
}

juce::Rectangle<int> MakuPage::cellRect(int cell) const
{
    const int part = cell / 8, col = cell % 8, y = kRowY + part * kRowH;
    if (col == 0) return {kColX, y, kNameW, kRowH - 2};
    if (col == 1) return {kColX + kNameW, y, kLevelW, kRowH - 2};
    return {kColX + kNameW + kLevelW + (col - 2) * kMacroW, y, kMacroW, kRowH - 2};
}

int MakuPage::cellAt(juce::Point<int> local) const
{
    const juce::Point<int> p(toLcd(local.x), toLcd(local.y));
    for (int part = 0; part < maku::kParts; ++part)
        for (int col = 0; col < 5; ++col)
            if (cellRect(part * 8 + col).contains(p)) return part * 8 + col;
    if (juce::Rectangle<int>(kScrX, kScrY, kScrW, kScrW).contains(p)) return 100;   // the screen
    return -1;
}

int MakuPage::cellValue(int cell) const
{
    const int part = cell / 8, col = cell % 8;
    if (part >= maku::kParts || col < 1 || col > 4) return 0;
    if (cell == m_drag && m_dragValue >= 0) return m_dragValue;
    return col == 1 ? m_ui.level[size_t(part)] : m_ui.macro[size_t(part)][size_t(col - 2)];
}

void MakuPage::setCellValue(int cell, int v)
{
    const int part = cell / 8, col = cell % 8;
    v = juce::jlimit(0, 127, v);
    if (col == 1) m_proc.makuCommand({maku::MakuCommand::Level, part, v});
    else if (col >= 2 && col <= 4) m_proc.makuCommand({maku::MakuCommand::Macro, part, col - 2, v});
    if (cell == m_drag) m_dragValue = v;
    repaint();
}

juce::String MakuPage::helpOf(int cell) const
{
    if (cell == 100) return "LO SCHERMO DI MAKU: ONDA, BPM, VOLUMI E LA PARTE A FUOCO; MOSTRA I GESTI MENTRE LI TIENI PREMUTI";
    if (cell < 0) return {};
    const int part = cell / 8, col = cell % 8;
    if (col == 0) return kPartHelp[part];
    if (col == 1) return juce::String(m_ui.part[size_t(part)].data()) + " LEVEL: VOLUME DELLA PARTE (ENCODER A-D). TRASCINA O ROTELLA";
    const juce::String name = m_ui.macroName[size_t(part)][size_t(col - 2)].data();
    return juce::String(m_ui.part[size_t(part)].data()) + " " + name
           + ": UN MACRO DI MAKU (ENCODER E-G SULLA PARTE A FUOCO). PIU' A DESTRA = PIU' INTENSO. TRASCINA O ROTELLA";
}

void MakuPage::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &m_like && e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        m.addItem(1, "Dimentica tutti i like (" + juce::String(m_ui.likes) + ")", m_ui.likes > 0);
        juce::Component::SafePointer<MakuPage> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_like), [safe](int id) {
            if (safe && id == 1) safe->m_proc.makuCommand({maku::MakuCommand::LikesClear});
        });
        return;
    }
    if (e.eventComponent != this) return;
    const int c = cellAt(e.getPosition());
    if (c < 0 || c == 100) return;
    if (c % 8 == 0) { m_proc.makuCommand({maku::MakuCommand::Focus, c / 8}); if (onFocus) onFocus(c / 8); return; }
    m_drag = c;
    m_dragStart = cellValue(c);
    m_dragValue = m_dragStart;
}

void MakuPage::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent != this || m_drag < 0) return;
    const float px = e.mods.isShiftDown() ? 8.0f : 2.0f;
    const int v = juce::jlimit(0, 127, m_dragStart + int(std::lround((e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / px)));
    if (v != m_dragValue) setCellValue(m_drag, v);
}

void MakuPage::mouseUp(const juce::MouseEvent&)
{
    m_drag = -1;
    m_dragValue = -1;
}

void MakuPage::mouseMove(const juce::MouseEvent& e)
{
    if (e.eventComponent == this) {
        const int c = cellAt(e.getPosition());
        if (c != m_hover) { m_hover = c; repaint(); }
        setHelp(helpOf(c));
        return;
    }
    if (auto* f = dynamic_cast<SeqField*>(e.eventComponent); f && f->helpFn) { setHelp(f->helpFn()); return; }
    if (e.eventComponent == &m_newWorld) setHelp(maku::kVerbs[7].help);
    else if (e.eventComponent == &m_like) setHelp("LIKE: RICORDA IL MONDO ATTUALE; I PROSSIMI NEW WORLD CRESCONO SUI TUOI GUSTI. TASTO DESTRO = DIMENTICA TUTTI I LIKE");
    else if (e.eventComponent == &m_flat || e.eventComponent == &m_sharp)
        setHelp("FLAT / SHARP: LA TONALITA' SCENDE / SALE DI UNA QUINTA (UN BEMOLLE / DIESIS IN PIU'): CAMBIA SOLO LA NOTA CHE L'ARMATURA SPOSTA");
}

void MakuPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.eventComponent != this) return;
    const int c = cellAt(e.getPosition());
    if (c < 0 || c == 100 || c % 8 == 0) return;
    m_wheel += w.deltaY;
    const int steps = int(m_wheel / 0.05f);
    if (steps == 0) return;
    m_wheel -= float(steps) * 0.05f;
    setCellValue(c, cellValue(c) + (w.isReversed ? -steps : steps));
}

void MakuPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    // status line: MAKU, its settings (child fields), what it is doing
    cv.fillRect(0, 0, 62, 14);
    cv.text(bold(), "MAKU", 4, 3, false);
    juce::String st;
    if (!m_ui.available) st = "NOT AVAILABLE";
    else if (m_proc.apvts.getRawParameterValue(maku::kOnId)->load() < 0.5f) st = "OFF";
    else st = (m_ui.running ? juce::String("PLAY ") : juce::String(m_ui.extClock ? "WAIT FOR PLAY " : "STOP ")) + juce::String(m_ui.bpm) + " BPM";
    cv.text(small(), st.toRawUTF8(), kLcdW - 4 - LcdCanvas::textWidth(small(), st.toRawUTF8()), 5);
    cv.dotsH(0, kLcdW - 1, 13);

    // MAKU's screen: a frame here, the pixels after the canvas
    cv.fillRect(kScrX - 2, kScrY - 2, kScrW + 4, kScrW + 4);
    const juce::String key = juce::String(mnm::gen::kNoteNames[juce::jlimit(0, 11, m_ui.root)]) + " " + maku::kMakuScaleNames[m_ui.scale & 15];
    cv.text(small(), ("WORLD " + juce::String(m_ui.world) + "   KEY " + key).toRawUTF8(), kScrX, kScrY + kScrW + 6);
    cv.text(tiny(), ("SEED " + juce::String::toHexString(int(m_ui.seed)).toUpperCase() + "   LIKES " + juce::String(m_ui.likes) + "/16"
                     + (m_ui.liked ? "  LIKED" : "") + (m_ui.drift ? "   DRIFT ON" : "")).toRawUTF8(), kScrX, kScrY + kScrW + 15);

    // the parts
    cv.fillRect(kColX, kTitleY, kColW, 10);
    cv.text(bold(), "PARTS", kColX + 3, kTitleY + 1, false);
    const juce::String hdr = "LEVEL     MACROS: DRAG / WHEEL";
    cv.text(tiny(), hdr.toRawUTF8(), kColX + kNameW + 2, kTitleY + 3, false);
    for (int part = 0; part < maku::kParts; ++part) {
        const int y = kRowY + part * kRowH;
        const bool focus = m_ui.focus == part;
        auto name = cellRect(part * 8);
        if (focus) cv.fillRect(name.getX(), name.getY(), name.getWidth() - 2, name.getHeight());
        cv.text(bold(), (juce::String(part + 1) + " " + juce::String(m_ui.part[size_t(part)].data())).toRawUTF8(), name.getX() + 2, y + 2, !focus);
        cv.text(tiny(), fit(tiny(), juce::String(m_ui.voice[size_t(part)].data()).isEmpty() ? juce::String(kPartRole[part]) : juce::String(m_ui.voice[size_t(part)].data()),
                            kNameW - 5).toRawUTF8(), name.getX() + 2, y + 13, !focus);
        cv.text(tiny(), kPartRole[part], name.getX() + 2, y + 20, !focus);
        if (m_hover == part * 8 && !focus) cv.dotsH(name.getX(), name.getRight() - 3, name.getBottom() - 1);
        for (int col = 1; col <= 4; ++col) {
            const auto r = cellRect(part * 8 + col);
            const int v = cellValue(part * 8 + col);
            const juce::String label = col == 1 ? juce::String("LEVEL") : juce::String(m_ui.macroName[size_t(part)][size_t(col - 2)].data());
            cv.dotsV(r.getX(), r.getY(), r.getBottom());
            cv.text(tiny(), label.toRawUTF8(), r.getX() + 3, y + 3);
            const int bw = r.getWidth() - 8, bx = r.getX() + 3, by = y + 11;
            outline(cv, bx, by, bw, 6);
            cv.fillRect(bx + 1, by + 1, (bw - 2) * v / 127, 4);
            cv.text(small(), juce::String(v).toRawUTF8(), bx, y + 20);
            if (m_hover == part * 8 + col || m_drag == part * 8 + col) cv.invertRect(r.getX() + 1, y + 19, r.getWidth() - 2, 7);
        }
        cv.dotsH(kColX, kColX + kColW - 1, y + kRowH - 2);
    }

    // the help line
    const juce::String help = m_help.isNotEmpty() ? m_help
        : !m_ui.available ? m_proc.makuError().toUpperCase()
        : juce::String("TASTI SINK..SCRAMBLE = GESTI (TIENI PREMUTO). DRIFT = MOTO PROPRIO. TASTI TRACK 1-4 = PARTE A FUOCO, LA TASTIERA LA SUONA");
    {
        juce::StringArray words = juce::StringArray::fromTokens(help, " ", "");
        juce::String line;
        int y = kHelpY;
        for (const auto& w : words) {
            const juce::String t = line.isEmpty() ? w : line + " " + w;
            if (LcdCanvas::textWidth(tiny(), t.toRawUTF8()) > kColW - 2 && line.isNotEmpty()) {
                cv.text(tiny(), line.toRawUTF8(), kColX, y);
                y += 7;
                line = w;
                if (y > kLcdH - 6) break;
            } else line = t;
        }
        if (y <= kLcdH - 6 && line.isNotEmpty()) cv.text(tiny(), line.toRawUTF8(), kColX, y);
    }
    cv.draw(g, 0, 0);

    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    const auto dst = juce::Rectangle<int>(kScrX * kScale, kScrY * kScale, kScrW * kScale, kScrW * kScale);
    if (m_ui.available) g.drawImage(m_screen, dst.toFloat(), juce::RectanglePlacement::stretchToFit);
    else { g.setColour(juce::Colours::black); g.fillRect(dst); }
}

} // namespace mnm::plugin::one
