# Monomodule Euclid: manuale utente (ReportLab). Genera docs/Monomodule-Euclid-Manuale.pdf:
#   python3 docs/manual/manual.py
# Servono reportlab e i font Poppins (Google Fonts) e DejaVu Sans Mono; le schermate in docs/manual/img vengono da
# mnm-uisnapshot (--euclid, --euclid-maku, MK_MIX=1 --euclid-maku), ritagliate per le pagine SEQ e MIX.
import math, random, os
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, 'img')
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.colors import HexColor
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle,
                                Flowable, NextPageTemplate, PageBreak, KeepTogether, CondPageBreak, Image)
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER

G = '/usr/share/fonts/truetype/google-fonts/'
pdfmetrics.registerFont(TTFont('Pop-L', G + 'Poppins-Light.ttf'))
pdfmetrics.registerFont(TTFont('Pop', G + 'Poppins-Regular.ttf'))
pdfmetrics.registerFont(TTFont('Pop-M', G + 'Poppins-Medium.ttf'))
pdfmetrics.registerFont(TTFont('Pop-B', G + 'Poppins-Bold.ttf'))
pdfmetrics.registerFont(TTFont('Pop-I', G + 'Poppins-Italic.ttf'))
pdfmetrics.registerFont(TTFont('Mono', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'))
pdfmetrics.registerFont(TTFont('Mono-B', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'))
from reportlab.lib.fonts import addMapping
addMapping('Pop', 0, 0, 'Pop'); addMapping('Pop', 1, 0, 'Pop-B'); addMapping('Pop', 0, 1, 'Pop-I'); addMapping('Pop', 1, 1, 'Pop-B')

INK = HexColor('#111310'); PAPER = HexColor('#F4F3EE'); NIGHT = HexColor('#0B0D09')
LIME = HexColor('#C8F53A'); LIME_D = HexColor('#4E6B00'); GREY = HexColor('#7C7F74'); RULE = HexColor('#D9D7CD')
CARD = HexColor('#E9E8E0'); CYAN = HexColor('#36E0D0'); MAG = HexColor('#FF4FA3')

W, H = A4
ML, MR, MT, MB = 20 * mm, 20 * mm, 24 * mm, 20 * mm
CW = W - ML - MR

# ---------- styles
def S(name, **kw):
    base = dict(fontName='Pop', fontSize=9.4, leading=14.2, textColor=INK)
    base.update(kw); return ParagraphStyle(name, **base)
body = S('body')
bodyS = S('bodyS', fontSize=8.4, leading=12.4)
lead = S('lead', fontName='Pop-L', fontSize=13, leading=19.5)
h2 = S('h2', fontName='Pop-B', fontSize=13.5, leading=18, spaceBefore=6, spaceAfter=4)
h3 = S('h3', fontName='Pop-M', fontSize=10.5, leading=14, spaceBefore=4, spaceAfter=2)
mono = S('mono', fontName='Mono', fontSize=7.6, leading=11, textColor=INK)
monoG = S('monoG', fontName='Mono', fontSize=7.2, leading=10, textColor=GREY)
cell = S('cell', fontSize=8.1, leading=11.2)
cellB = S('cellB', fontName='Pop-M', fontSize=8.1, leading=11.2)
cellM = S('cellM', fontName='Mono-B', fontSize=7.6, leading=11.2)
tip = S('tip', fontSize=8.8, leading=13)

def P(t, st=body): return Paragraph(t, st)
def k(t): return '<font name="Mono-B" size="8" color="#4E6B00">%s</font>' % t   # a control name

# ---------- page decoration
def header_footer(c, doc):
    c.saveState()
    c.setFillColor(PAPER); c.rect(0, 0, W, H, fill=1, stroke=0)
    c.setStrokeColor(RULE); c.setLineWidth(0.5); c.line(ML, H - 15 * mm, W - MR, H - 15 * mm)
    # footer: page number in decimal and hex, a small euclid strip
    n = doc.page
    c.setFont('Mono-B', 8); c.setFillColor(INK); c.drawRightString(W - MR, 11 * mm, '%02d' % n)
    c.setFont('Mono', 6.5); c.setFillColor(GREY); c.drawRightString(W - MR - 9 * mm, 11 * mm, '0x%02X' % n)
    steps, pulses = 16, (n * 3) % 11 + 3
    for i in range(steps):
        hit = (i * pulses) % steps < pulses
        c.setFillColor(LIME_D if hit else RULE)
        c.rect(ML + i * 3.2 * mm, 10.6 * mm, 2.2 * mm, 2.2 * mm if hit else 0.8 * mm, fill=1, stroke=0)
    c.restoreState()

def header_text(c, doc):
    c.saveState(); c.setFont('Mono', 6.8); c.setFillColor(GREY)
    c.drawString(ML, H - 13 * mm, 'MONOMODULE EUCLID  //  MANUALE UTENTE')
    c.drawRightString(W - MR, H - 13 * mm, getattr(doc, 'section', '').upper())
    c.restoreState()

class Doc(BaseDocTemplate):
    def afterFlowable(self, f):
        if isinstance(f, SectionHead):
            self.section = f.title
            self.notify('TOCEntry', (0, f.title, self.page))

# ---------- flowables
class SectionHead(Flowable):
    def __init__(self, num, title, kicker=''):
        super().__init__(); self.num, self.title, self.kicker = num, title, kicker
    def wrap(self, aw, ah): self.w = aw; return aw, 30 * mm
    def draw(self):
        c = self.canv
        c.setFillColor(INK); c.roundRect(0, 4 * mm, 22 * mm, 22 * mm, 3 * mm, fill=1, stroke=0)
        c.setFillColor(LIME); c.setFont('Pop-B', 22); c.drawCentredString(11 * mm, 11.5 * mm, self.num)
        c.setFillColor(INK); c.setFont('Pop-B', 22); c.drawString(27 * mm, 14.5 * mm, self.title)
        c.setFont('Mono', 7.5); c.setFillColor(GREY); c.drawString(27.3 * mm, 8 * mm, self.kicker.upper())
        c.setStrokeColor(LIME_D); c.setLineWidth(1.2); c.line(27 * mm, 5.2 * mm, 27 * mm + 18 * mm, 5.2 * mm)

class Callout(Flowable):
    """Tip box: lime bar, title, paragraph."""
    def __init__(self, title, text, dark=False):
        super().__init__(); self.title, self.dark = title, dark
        self.p = Paragraph(text, S('c', fontSize=8.8, leading=13, textColor=PAPER if dark else INK))
    def wrap(self, aw, ah):
        self.w = aw; _, ph = self.p.wrap(aw - 12 * mm, ah); self.ph = ph; self.h = ph + 13 * mm; return aw, self.h
    def draw(self):
        c = self.canv
        c.setFillColor(INK if self.dark else CARD); c.roundRect(0, 0, self.w, self.h, 2.5 * mm, fill=1, stroke=0)
        c.setFillColor(LIME if self.dark else LIME_D); c.rect(0, 0, 1.6 * mm, self.h, fill=1, stroke=0)
        c.setFont('Mono-B', 7.2); c.setFillColor(LIME if self.dark else LIME_D)
        c.drawString(6 * mm, self.h - 6.5 * mm, self.title.upper())
        self.p.drawOn(c, 6 * mm, 4.5 * mm)

def euclid(steps, pulses, rot=0):
    return [((((i - rot) % steps) * pulses) % steps) < pulses if pulses > 0 else False for i in range(steps)]

def draw_necklace(c, cx, cy, r, steps, pulses, rot=0, ink=INK, accent=LIME_D, acc=0, play=None, lw=0.8, label=True):
    pat = euclid(steps, pulses, rot); ac = euclid(steps, acc, rot) if acc else [False] * steps
    pts = []
    for i in range(steps):
        a = math.pi / 2 - 2 * math.pi * i / steps
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    c.setStrokeColor(RULE if ink == INK else HexColor('#2A2E24')); c.setLineWidth(0.4); c.circle(cx, cy, r, stroke=1, fill=0)
    hits = [pts[i] for i in range(steps) if pat[i]]
    if len(hits) > 1:
        c.setStrokeColor(accent); c.setLineWidth(lw)
        p = c.beginPath(); p.moveTo(*hits[0])
        for q in hits[1:]: p.lineTo(*q)
        p.close(); c.drawPath(p, stroke=1, fill=0)
    for i, (x, y) in enumerate(pts):
        if pat[i]:
            s = 2.3 if ac[i] else 1.5
            c.setFillColor(ink); c.rect(x - s, y - s, 2 * s, 2 * s, fill=1, stroke=0)
        else:
            c.setFillColor(GREY); c.circle(x, y, 0.55, fill=1, stroke=0)
        if play == i:
            c.setStrokeColor(accent); c.setLineWidth(0.8); c.rect(x - 3.6, y - 3.6, 7.2, 7.2, fill=0, stroke=1)
    if label:
        c.setFillColor(ink); c.setFont('Mono-B', 8); c.drawCentredString(cx, cy - 2.5, 'E(%d,%d)' % (pulses, steps))

class NecklaceRow(Flowable):
    def __init__(self, specs, height=48 * mm):
        super().__init__(); self.specs, self.h = specs, height
    def wrap(self, aw, ah): self.w = aw; return aw, self.h
    def draw(self):
        c = self.canv; n = len(self.specs); cw = self.w / n
        for j, (steps, pulses, rot, cap) in enumerate(self.specs):
            cx = cw * j + cw / 2; cy = self.h - 22 * mm
            draw_necklace(c, cx, cy, 16 * mm, steps, pulses, rot, play=None)
            # strip
            pat = euclid(steps, pulses, rot); sw = min(2.6 * mm, (cw - 8 * mm) / steps)
            x0 = cx - sw * steps / 2
            for i in range(steps):
                c.setFillColor(INK if pat[i] else RULE)
                c.rect(x0 + i * sw, 7.5 * mm, sw * 0.8, 2.6 * mm if pat[i] else 0.9 * mm, fill=1, stroke=0)
            c.setFont('Pop', 7.6); c.setFillColor(GREY); c.drawCentredString(cx, 2 * mm, cap)

class Flow(Flowable):
    """Signal flow: DAW -> sequencer -> 6 tracks DSP -> outputs."""
    def wrap(self, aw, ah): self.w = aw; return aw, 40 * mm
    def draw(self):
        c = self.canv
        boxes = [('DAW', 'PLAY / BPM / POSIZIONE\nMIDI CH 1-6, 16'), ('SEQUENCER', 'EUCLIDE + PROBABILITÀ\nNOTE + P-LOCK'),
                 ('6 TRACCE', 'DSP56300 EMULATO\n22 MACCHINE'), ('USCITE', '6 STEREO O\nBUS AB / CD / EF')]
        n = len(boxes); gap = 9 * mm; bw = (self.w - gap * (n - 1)) / n; bh = 24 * mm; y = 8 * mm
        for i, (t, sub) in enumerate(boxes):
            x = i * (bw + gap)
            dark = i in (1, 2)
            c.setFillColor(INK if dark else CARD); c.roundRect(x, y, bw, bh, 2.5 * mm, fill=1, stroke=0)
            c.setFillColor(LIME if dark else INK); c.setFont('Pop-B', 11); c.drawString(x + 4 * mm, y + bh - 8.5 * mm, t)
            c.setFillColor(PAPER if dark else GREY); c.setFont('Mono', 6.6)
            for li, line in enumerate(sub.split('\n')): c.drawString(x + 4 * mm, y + bh - 14.5 * mm - li * 3.6 * mm, line)
            if i < n - 1:
                ax = x + bw + 1.5 * mm; ay = y + bh / 2
                c.setStrokeColor(LIME_D); c.setFillColor(LIME_D); c.setLineWidth(1.2)
                c.line(ax, ay, ax + gap - 4 * mm, ay)
                p = c.beginPath(); p.moveTo(ax + gap - 3 * mm, ay); p.lineTo(ax + gap - 5 * mm, ay + 1.4 * mm); p.lineTo(ax + gap - 5 * mm, ay - 1.4 * mm); p.close()
                c.drawPath(p, fill=1, stroke=0)
        c.setFont('Mono', 6.6); c.setFillColor(GREY)
        c.drawString(0, 2 * mm, 'Tutto avviene dentro il plugin: nessun MIDI esce verso la DAW, nessun clip da registrare per sentire il sequencer.')

class Shot(Flowable):
    """The SEQUENCER page screenshot with numbered markers."""
    MARKS = [(1, 128, -14), (2, 760, -14), (3, 14, 312), (4, 620, 175), (5, 935, 70), (6, 380, 340), (7, 778, 340), (8, 1180, 340), (9, 1290, 335), (10, 1345, 582)]
    def __init__(self, path, w): super().__init__(); self.path, self.w = path, w; self.h = w * 600 / 1378
    def wrap(self, aw, ah): return self.w, self.h + 7 * mm
    def draw(self):
        c = self.canv; s = self.w / 1378.0
        c.setFillColor(INK); c.roundRect(-2 * mm, -2 * mm, self.w + 4 * mm, self.h + 8.5 * mm, 3 * mm, fill=1, stroke=0)
        c.drawImage(self.path, 0, 0, self.w, self.h, mask=None)
        for n, x, y in self.MARKS:
            px, py = x * s, self.h - y * s
            c.setFillColor(LIME_D); c.circle(px, py, 3.1 * mm, fill=1, stroke=0)
            c.setFillColor(LIME); c.circle(px, py, 2.6 * mm, fill=1, stroke=0)
            c.setFillColor(INK); c.setFont('Pop-B', 8); c.drawCentredString(px, py - 2.8, str(n))

class ShotM(Flowable):
    """A screenshot (iw x ih px) with numbered markers at pixel positions."""
    def __init__(self, path, w, iw, ih, marks): super().__init__(); self.path, self.w, self.iw, self.marks = path, w, iw, marks; self.h = w * ih / iw
    def wrap(self, aw, ah): return self.w, self.h + 3 * mm
    def draw(self):
        c = self.canv; s = self.w / float(self.iw)
        c.setFillColor(INK); c.roundRect(-2 * mm, -2 * mm, self.w + 4 * mm, self.h + 4 * mm, 3 * mm, fill=1, stroke=0)
        c.drawImage(self.path, 0, 0, self.w, self.h, mask=None)
        for n, x, y in self.marks:
            px, py = x * s, self.h - y * s
            c.setFillColor(LIME_D); c.circle(px, py, 3.1 * mm, fill=1, stroke=0)
            c.setFillColor(LIME); c.circle(px, py, 2.6 * mm, fill=1, stroke=0)
            c.setFillColor(INK); c.setFont('Pop-B', 8); c.drawCentredString(px, py - 2.8, str(n))

def num_badge(n):
    return '<font name="Pop-B" size="8" color="#111310"><span backColor="#C8F53A">&nbsp;%s&nbsp;</span></font>' % n

def table(rows, widths, head=True, zebra=True, first_mono=True, compact=False):
    data = []
    for ri, r in enumerate(rows):
        out = []
        for ci, v in enumerate(r):
            if ri == 0 and head: out.append(Paragraph(v, S('th', fontName='Mono-B', fontSize=7, leading=9, textColor=LIME)))
            elif ci == 0 and first_mono: out.append(Paragraph(v, cellM))
            else: out.append(Paragraph(v, cell))
        data.append(out)
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    st = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 5), ('RIGHTPADDING', (0, 0), (-1, -1), 5),
          ('TOPPADDING', (0, 0), (-1, -1), 2.4 if compact else 4), ('BOTTOMPADDING', (0, 0), (-1, -1), 2.6 if compact else 4),
          ('LINEBELOW', (0, 1), (-1, -1), 0.4, RULE)]
    if head: st += [('BACKGROUND', (0, 0), (-1, 0), INK), ('TOPPADDING', (0, 0), (-1, 0), 5), ('BOTTOMPADDING', (0, 0), (-1, 0), 5)]
    if zebra:
        for i in range(1 if head else 0, len(rows)):
            if i % 2 == 0: st.append(('BACKGROUND', (0, i), (-1, i), HexColor('#ECEBE4')))
    t.setStyle(TableStyle(st)); return t

def steps_list(items):
    data = []
    for i, (title, text) in enumerate(items, 1):
        badge = Paragraph('<font name="Pop-B" size="15" color="#C8F53A">%02d</font>' % i, S('b', alignment=TA_CENTER, leading=20))
        data.append([badge, [Paragraph('<font name="Pop-B" size="10.5">%s</font>' % title, S('st', leading=15)), Paragraph(text, body)]])
    t = Table(data, colWidths=[15 * mm, CW - 15 * mm])
    st = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('BACKGROUND', (0, 0), (0, -1), INK), ('TOPPADDING', (0, 0), (-1, -1), 6),
          ('BOTTOMPADDING', (0, 0), (-1, -1), 7), ('LEFTPADDING', (1, 0), (1, -1), 8), ('LINEBELOW', (0, 0), (-1, -2), 0.6, PAPER),
          ('LINEBELOW', (1, 0), (1, -2), 0.4, RULE)]
    t.setStyle(TableStyle(st)); return t

# ---------- cover and easter egg (full-page canvases)
def cover(c, doc):
    c.saveState()
    c.setFillColor(NIGHT); c.rect(0, 0, W, H, fill=1, stroke=0)
    # dot grid
    c.setFillColor(HexColor('#1C2017'))
    for x in range(0, int(W), 9):
        for y in range(0, int(H), 9): c.circle(x + 4, y + 4, 0.45, fill=1, stroke=0)
    # nested euclid necklaces, the six default tracks
    cx, cy = W * 0.62, H * 0.58
    specs = [(16, 5, 0, 0), (13, 6, 3, 0), (11, 4, 0, 3), (16, 7, 2, 0), (32, 5, 1, 0), (12, 5, 0, 0)]
    cols = [LIME, CYAN, LIME, MAG, LIME, CYAN]
    for j, (st, pu, ro, ac) in enumerate(specs):
        r = (34 + j * 15) * mm * 0.62
        c.setLineWidth(0.35); c.setStrokeColor(HexColor('#2E3527')); c.circle(cx, cy, r, stroke=1, fill=0)
        pat = euclid(st, pu, ro)
        pts = [(cx + r * math.cos(math.pi / 2 - 2 * math.pi * i / st), cy + r * math.sin(math.pi / 2 - 2 * math.pi * i / st)) for i in range(st)]
        hits = [pts[i] for i in range(st) if pat[i]]
        c.setStrokeColor(cols[j]); c.setLineWidth(0.9 if j % 2 == 0 else 0.6)
        p = c.beginPath(); p.moveTo(*hits[0]); [p.lineTo(*q) for q in hits[1:]]; p.close(); c.drawPath(p, stroke=1, fill=0)
        for i, (x, y) in enumerate(pts):
            if pat[i]: c.setFillColor(cols[j]); c.rect(x - 1.7, y - 1.7, 3.4, 3.4, fill=1, stroke=0)
            else: c.setFillColor(HexColor('#4A5240')); c.circle(x, y, 0.6, fill=1, stroke=0)
    c.setFillColor(LIME); c.circle(cx, cy, 2.2, fill=1, stroke=0)
    # title block
    c.setFillColor(LIME); c.setFont('Mono-B', 8); c.drawString(ML, H - 26 * mm, 'MANUALE UTENTE  ·  VST3 / AU / STANDALONE  ·  v1.0')
    c.setFillColor(PAPER); c.setFont('Pop-L', 30); c.drawString(ML, 78 * mm, 'Monomodule')
    c.setFont('Pop-B', 58); c.setFillColor(LIME); c.drawString(ML - 1.5, 56 * mm, 'EUCLID')
    c.setFont('Pop', 11); c.setFillColor(PAPER)
    c.drawString(ML, 44 * mm, 'Una Monomachine emulata a livello di chip,')
    c.drawString(ML, 38.5 * mm, 'suonata da un sequencer euclideo generativo.')
    c.setStrokeColor(LIME); c.setLineWidth(1.4); c.line(ML, 31 * mm, ML + 30 * mm, 31 * mm)
    c.setFont('Mono', 7); c.setFillColor(GREY)
    c.drawString(ML, 24 * mm, 'BASATO SU MONOMODULE DI SHNOLK (AGPL v3)  ·  MOTORE DSP: DSP56300 EMULATO')
    c.drawString(ML, 19.5 * mm, '6 TRACCE  ·  22 MACCHINE  ·  4 LANE DI P-LOCK PER TRACCIA  ·  SYNC SAMPLE-ACCURATE')
    c.restoreState()

def easter(c, doc):
    c.saveState()
    c.setFillColor(NIGHT); c.rect(0, 0, W, H, fill=1, stroke=0)
    rnd = random.Random(1977)
    # perspective grid floor
    c.setStrokeColor(HexColor('#1E3A1A')); c.setLineWidth(0.5)
    hy = H * 0.25; vx = W / 2
    for i in range(-24, 25):
        c.line(vx + i * 4, hy, vx + i * 48 * mm / 2.0, 0)
    y, d = hy, 2.0
    while y > 0:
        c.setStrokeColor(HexColor('#1E3A1A')); c.line(0, y, W, y); d *= 1.28; y -= d
    # horizon glow bands
    for i, col in enumerate(['#1F2A0C', '#2B3C0E', '#3E5712', '#5A7D16']):
        c.setFillColor(HexColor(col)); c.rect(0, hy + i * 1.2, W, 1.0, fill=1, stroke=0)
    # the sun: a euclid machine, rings of the seed
    cx, cy = W / 2, H * 0.60
    for j in range(9):
        steps = [16, 13, 11, 19, 7, 23, 12, 29, 32][j]; pulses = [5, 6, 4, 7, 3, 9, 5, 11, 13][j]
        r = (18 + j * 7.4) * mm
        pat = euclid(steps, pulses, j)
        col = [LIME, CYAN, MAG][j % 3]
        c.setStrokeColor(HexColor('#22281C')); c.setLineWidth(0.3); c.circle(cx, cy, r, stroke=1, fill=0)
        for i in range(steps):
            a0 = math.pi / 2 - 2 * math.pi * i / steps
            if pat[i]:
                a1 = a0 - 2 * math.pi / steps * 0.62
                c.setStrokeColor(col); c.setLineWidth(2.2 if j % 2 == 0 else 1.2)
                p = c.beginPath()
                for t in range(9):
                    a = a0 + (a1 - a0) * t / 8
                    (p.moveTo if t == 0 else p.lineTo)(cx + r * math.cos(a), cy + r * math.sin(a))
                c.drawPath(p, stroke=1, fill=0)
    # binary ring: the secret
    secret = 'SEED 4242 LOOP 8 BARS EVOLVE 66 :: PLAY LOUD'
    bits = ''.join(format(ord(ch), '08b') for ch in secret)
    r = 84 * mm
    c.setFont('Mono', 4.4)
    for i, b in enumerate(bits):
        a = math.pi / 2 - 2 * math.pi * i / len(bits)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        c.saveState(); c.translate(x, y); c.rotate(math.degrees(a) - 90)
        c.setFillColor(LIME if b == '1' else HexColor('#3A4430')); c.drawCentredString(0, 0, b); c.restoreState()
    # core
    c.setFillColor(NIGHT); c.circle(cx, cy, 15 * mm, fill=1, stroke=0)
    c.setStrokeColor(LIME); c.setLineWidth(1); c.circle(cx, cy, 15 * mm, fill=0, stroke=1)
    c.setFillColor(LIME); c.setFont('Pop-B', 16); c.drawCentredString(cx, cy + 1, 'E∞')
    c.setFont('Mono', 5.5); c.setFillColor(GREY); c.drawCentredString(cx, cy - 6, 'SIGNAL 1977')
    # scanlines
    c.setFillColor(HexColor('#000000'))
    c.setFillAlpha(0.18)
    yy = 0
    while yy < H: c.rect(0, yy, W, 0.5, fill=1, stroke=0); yy += 2.2
    c.setFillAlpha(1)
    # glitch bars
    for _ in range(7):
        gy = rnd.uniform(H * 0.45, H * 0.85); gw = rnd.uniform(20, 120) * mm; gx = rnd.uniform(0, W - gw)
        c.setFillColor([LIME, CYAN, MAG][rnd.randrange(3)]); c.setFillAlpha(0.22); c.rect(gx, gy, gw, rnd.uniform(0.6, 2.2), fill=1, stroke=0)
    c.setFillAlpha(1)
    # text
    c.setFillColor(LIME); c.setFont('Mono-B', 8); c.drawString(ML, H - 22 * mm, '>> TRASMISSIONE INTERCETTATA')
    c.setFillColor(GREY); c.setFont('Mono', 6.8)
    c.drawString(ML, H - 27 * mm, 'ORIGINE: SCONOSCIUTA  ·  FORMATO: 8 BIT ASCII  ·  ANELLO ESTERNO, ORARIO, DA ORE 12')
    c.setFillColor(PAPER); c.setFont('Pop-B', 30); c.drawString(ML, 52 * mm, 'Pagina nascosta.')
    c.setFont('Pop-L', 11); c.setFillColor(PAPER)
    c.drawString(ML, 44 * mm, "L'anello di zeri e uni intorno al sole contiene una patch.")
    c.drawString(ML, 38.5 * mm, 'Decodificalo (8 bit per carattere) e inseriscila nella barra in alto.')
    c.setFont('Mono', 6.6); c.setFillColor(GREY)
    c.drawString(ML, 26 * mm, 'SUGGERIMENTO: IL PRIMO CARATTERE È 01010011.')
    c.drawRightString(W - MR, 14 * mm, 'EOF  //  0x%02X' % doc.page)
    c.restoreState()

# ---------- content
story = []
story.append(NextPageTemplate('body')); story.append(PageBreak())

# 01 panoramica
story += [SectionHead('01', 'Panoramica', 'Cosa è Monomodule Euclid'),
          P("Monomodule Euclid è uno strumento VST3 (anche AU e standalone) che unisce due cose: una "
            "<b>Elektron Monomachine completa a sei tracce</b>, emulata facendo girare il codice DSP originale, e un "
            "<b>sequencer euclideo generativo</b> che la suona da solo, agganciato al trasporto della tua DAW.", lead),
          Spacer(1, 4 * mm),
          P("L'obiettivo è generare ritmi e melodie nello spirito della musica Warp, in particolare Autechre: pattern che si sfasano, "
            "colpi che rimbalzano, timbri che cambiano a ogni trig. Ogni nota resta comunque dentro un intervallo e una scala "
            "che scegli tu, e ogni manopola si muove solo dentro una finestra di valori, quindi il risultato resta musicale."),
          Spacer(1, 5 * mm), Flow(), Spacer(1, 5 * mm)]
ov = [['IN BREVE', ''],
      ['MOTORE', 'Il programma DSP originale della Monomachine (OS 1.32B) gira istruzione per istruzione in un Motorola DSP56300 emulato. Stesso suono dell\'hardware.'],
      ['6 TRACCE', 'Come Monomodule Six: sei tracce mono, ognuna con una delle 22 macchine, routing OUT BUS e ingressi FX.'],
      ['SEQUENCER', 'Un pattern euclideo per traccia, con rate indipendenti, probabilità, condizioni, ratchet, swing e drift.'],
      ['NOTE', 'Sempre in un range LOW–HIGH e in una scala (22 scale), con cinque modi: FIXED, RANDOM, WALK, TURING, CYCLE.'],
      ['P-LOCK', 'Quattro lane per traccia bloccano qualsiasi manopola SYN, AMP, FILT o EFX a ogni trig, dentro una finestra MIN–MAX.'],
      ['SYNC', 'Segue play, tempo, posizione e loop della DAW con precisione al campione. Il bounce suona identico al play.'],
      ['MAKU', 'Un secondo motore, ambient: cassa, drone, shimmer e arpeggio in un "mondo" casuale, a tempo con la DAW e nella stessa tonalità del sequencer (capitolo 08).']]
story += [table(ov, [26 * mm, CW - 26 * mm]), Spacer(1, 5 * mm),
          Callout('Prima di iniziare', "Il plugin non contiene software Elektron. Al primo avvio chiede il file "
                  "<b>Elektron_SFX6-60_OS1.32B.syx</b>, scaricabile gratis dal sito Elektron: apri la pagina <b>SYNTH</b>, "
                  "clicca <b>Select OS File…</b> e scegli il file. Lo fai una volta sola per tutti i plugin Monomodule. "
                  "Finché manca, il sequencer resta fermo e la riga d'aiuto lo segnala.")]

# 02 monomachine
story += [PageBreak(), SectionHead('02', 'Il motore Monomachine', 'Pagina SYNTH'),
          P("La pagina <b>SYNTH</b> (tasto SYNTH) porta sul display l'editor di Monomodule Six: i sei tasti traccia "
            "(numero, macchina, LED di attività, LOCK e MUTE) e le pagine della traccia selezionata, tutte visibili insieme: "
            "<b>SYNTHESIS</b> (le otto manopole della macchina), <b>AMPLIFICATION</b>, <b>FILTER</b>, <b>EFFECTS</b> e i tre "
            "<b>LFO</b>. Ogni valore è un byte 0–127 identico al kit hardware: un click sul valore lo fa digitare, il blocco "
            "macchina in alto apre il selettore delle 22 macchine."),
          Spacer(1, 4 * mm)]
fam = [['FAMIGLIA', 'MACCHINE', 'CARATTERE'],
       ['GND', 'GND, SIN, NOIS', 'Silenzio, sinusoide, rumore bianco: basi e percussioni minimali.'],
       ['SWAVE', 'SAW, PULS, ENS', 'Oscillatori analogici sovrapposti (unison), ensemble di archi. Pad e accordi spessi.'],
       ['SID', '6581', 'Emulazione del chip del Commodore 64: grana, pulse width, modulazioni.'],
       ['DPRO', 'WAVE, BBOX, DDRW, DENS', 'Forme d\'onda digitali crude e BBOX, un set di campioni di batteria (un suono per tasto).'],
       ['FM+', 'STAT, PAR, DYN', 'FM complessa resa semplice: metalli, campane, bassi percussivi.'],
       ['VO', 'VO-6', 'Sintesi vocale a formanti: vocali, consonanti, sussurri.'],
       ['FX', 'THRU, REVERB, CHORUS, DYNAMIX, RINGMOD, PHASER, FLANGER', "Effetti: elaborano la traccia precedente (NEIBOR), il side-chain o un bus."]]
story += [table(fam, [18 * mm, 48 * mm, CW - 66 * mm], compact=True), Spacer(1, 4 * mm)]
pages = [['PAGINA', 'COSA CONTROLLA'],
         ['SYNTHESIS', 'Le 8 manopole della macchina (A–H): il loro significato cambia con la macchina, come sull\'hardware.'],
         ['AMP', 'ATK, HOLD, DEC, REL (inviluppo), DIST, VOL, PAN, PORT (portamento).'],
         ['FILT', 'BASE e WDTH (filtro passa-banda), HPQ, LPQ, inviluppo del filtro ATK/DEC, offset BOFS/WOFS.'],
         ['EFX', 'EQ (EQF, EQG), sample rate reduction SRR, delay (DTIM, DSND, DFB, DBAS, DWID).'],
         ['LFO 1–3', 'Pagina e destinazione, trig, forma d\'onda, moltiplicatore, velocità, interlace, profondità. Seguono il BPM.'],
         ['ROUTING', 'Per traccia: OUT BUS AB/CD/EF; per le macchine FX: ingresso NEIBOR, INP A/B/AB o BUS. OUTPUTS: per traccia o bus.']]
story += [KeepTogether([P('Le pagine di ogni traccia', h3), table(pages, [22 * mm, CW - 22 * mm], compact=True)]), Spacer(1, 4 * mm)]
kit = [['TRACCIA', 'MACCHINA', 'RUOLO NEL KIT DI PARTENZA'],
       ['T1', 'DPRO BBOX', 'Kick (tasto 48), 5 colpi su 16, qualche roll in DECEL e retrig casuali.'],
       ['T2', 'DPRO BBOX', 'Colpi di batteria a caso sui tasti 49–58, 6 su 13 contro la T1, ratchet e drift.'],
       ['T3', 'FM+ DYN', 'Metallico in quintine (1/20), melodia TURING da G3 a G5 che muta piano.'],
       ['T4', 'SID 6581', 'Basso in WALK tra G1 e D3, filtro mosso da una SINE lenta.'],
       ['T5', 'SWAVE SAW', 'Accordi in ottavi: ciclo di 5 note contro 32 step, mandate al delay.'],
       ['T6', 'FX REVERB', 'Riverbero sulla T5 (ingresso NEIBOR); il suo sequencer è spento.']]
story += [KeepTogether([P('Il kit di partenza', h3),
                        P('All\'apertura il plugin è già un brano: premi play nella DAW e suona. Scala globale: D frigio.', bodyS),
                        Spacer(1, 2 * mm), table(kit, [18 * mm, 26 * mm, CW - 44 * mm], compact=True)])]

# 03 sequencer concept
story += [PageBreak(), SectionHead('03', 'Il sequencer euclideo', 'Concetto'),
          P("Un ritmo euclideo distribuisce <b>N colpi</b> il più uniformemente possibile su <b>M step</b>. Con pochi numeri "
            "nascono i ritmi di mezzo mondo: E(3,8) è il tresillo, E(5,16) un groove sincopato, E(7,12) una campana africana. "
            "Disegnati in cerchio, i colpi formano un poligono: è la <i>collana</i> che vedi nella pagina SEQ."),
          Spacer(1, 3 * mm),
          NecklaceRow([(8, 3, 0, 'tresillo'), (16, 5, 0, 'groove sincopato'), (12, 7, 0, 'campana'), (13, 6, 3, 'zoppo, ruotato di 3')]),
          Spacer(1, 4 * mm),
          P("Il carattere \"alieno\" arriva sovrapponendo tracce con lunghezze e rate diversi. Una traccia di 13 step contro una di 16 "
            "torna allineata solo dopo 208 step; una in quintine (1/20) contro una in sedicesimi scivola continuamente sulla battuta. "
            "Sopra a questa base il sequencer aggiunge il caso, sempre controllato:"),
          Spacer(1, 2 * mm)]
layers = [['STRATO', 'COSA AGGIUNGE'],
          ['RITMO', 'STEPS, PULSES, ROTATE, RATE per traccia, più un secondo euclideo per gli accenti.'],
          ['PROBABILITÀ', 'CHANCE: ogni colpo suona solo con una certa probabilità. CONDITION: regole stile Elektron (1:2, PRE, NEI…).'],
          ['MICRO-TEMPO', 'RATCHET: il colpo si ripete dentro lo step, con forme che accelerano o rimbalzano. SWING e DRIFT spostano i colpi.'],
          ['ALTEZZA', 'Note scelte da cinque generatori, sempre in range e in scala. HOLD stabilizza le frasi.'],
          ['TIMBRO', 'Quattro lane di p-lock per traccia: a ogni trig una manopola prende un valore nuovo dentro MIN–MAX.'],
          ['FORMA', 'SEED decide tutto il caso. LOOP lo fa ripetere ogni N battute, EVOLVE lo fa mutare a ogni giro.']]
story += [table(layers, [28 * mm, CW - 28 * mm]), Spacer(1, 4 * mm),
          Callout('Caso deterministico', "Ogni decisione casuale è calcolata da <b>SEED</b> e dalla posizione nel brano, non da un dado "
                  "lanciato in tempo reale. Se riascolti la battuta 17 senti la stessa cosa; il bounce offline è identico al play. "
                  "Cambia SEED (o premi <b>DICE</b>) per una tessitura nuova.", dark=True)]

# 04 interface
story += [PageBreak(), SectionHead('04', "L'interfaccia", 'Il pannello'),
          P("Monomodule Euclid si presenta come una groovebox hardware: al centro il display, intorno tasti in gomma con LED, "
            "otto encoder, sedici tasti trig e una tastiera. Il display cambia pagina (SEQ, SYNTH, MAKU, MIX) e il pannello "
            "intorno si adatta a quella pagina."), Spacer(1, 3 * mm),
          ShotM(os.path.join(IMG, 'panel-seq.png'), CW, 1994, 1040,
                [(1, 110, 215), (2, 110, 465), (3, 110, 650), (4, 760, 128), (5, 920, 400), (6, 1805, 215), (7, 1805, 450),
                 (8, 1805, 650), (9, 920, 830), (10, 110, 810), (11, 640, 960), (12, 110, 965), (13, 1500, 940)]),
          Spacer(1, 4 * mm)]
iface = [['N', 'ZONA', 'COSA FA'],
         ['1', 'TRACK 1–6', 'Selezionano la traccia (LED: lampeggia a ogni trig). Encoder, trig e tastiera lavorano sulla traccia selezionata.'],
         ['2', 'FUNCTION', '<b>SYNTH</b> e <b>SEQ</b>: la pagina del display. <b>GRID</b>: la traccia passa da EUCLID (ritmo generato) a GRID (note scritte a mano). '
                           '<b>REC</b>: le note che suoni si scrivono negli step. <b>CLEAR</b>: in GRID toglie tutti i trig, in EUCLID i mute. '
                           '<b>EUC > GRID</b>: copia il ritmo euclideo attuale negli step GRID, da rifinire.'],
         ['3', 'AMBIENT', '<b>MAKU</b>, <b>MAKU ON</b> e <b>MIX</b> (capitolo 08).'],
         ['4', 'Display', 'La pagina scelta. In SEQ: riga di stato, tracce, lane, pagine della traccia (pagina seguente).'],
         ['5', 'Riga d\'aiuto', 'Sotto il display: spiega il controllo sotto il mouse, in italiano.'],
         ['6', 'DATA ENTRY', 'Otto encoder sulla pagina scelta coi tasti PAGE; sopra il nome, sotto il valore.'],
         ['7', 'PAGE', '<b>SYN AMP FILT EFX LFO 1–3</b>: le manopole della Monomachine. <b>RHYTHM</b> e <b>MOTION</b> (LED arancio): quelle del sequencer.'],
         ['8', 'Trasporto', 'PLAY battuta.tempo o STOP, BPM, REC attivo e lo stato di MAKU.'],
         ['9', 'TRIG 1–16', 'In EUCLID mostrano i colpi (click = mute dello step). In GRID scrivono le note (sotto). La barretta sotto = durata.'],
         ['10', 'STEP PAGE', 'Step 1–16 o 17–32 sui tasti trig; il LED lampeggia dove sta suonando.'],
         ['11', 'Tastiera', 'Due ottave: suona la traccia; in GRID scrive la nota nello step selezionato.'],
         ['12', 'OCTAVE', 'Sposta la tastiera di un\'ottava (C3 = default).'],
         ['13', 'Suggerimenti', 'Cosa fanno, nel modo attuale, i trig e la tastiera.']]
story += [table(iface, [9 * mm, 24 * mm, CW - 33 * mm], compact=True)]

story += [PageBreak(), SectionHead('04', "L'interfaccia", 'La pagina SEQ sul display'),
          ShotM(os.path.join(IMG, 'seq-lcd.png'), CW, 1378, 630,
                [(1, 64, 22), (2, 664, 22), (3, 64, 184), (4, 404, 154), (5, 1164, 69), (6, 204, 339), (7, 604, 339), (8, 1004, 339),
                 (9, 1289, 394), (10, 1064, 584)]), Spacer(1, 5 * mm)]
seqz = [['N', 'ZONA', 'COSA FA'],
        ['1', 'SEQ Tn', 'La traccia selezionata.'],
        ['2', 'Barra globale', 'SEED con DICE, LOOP, EVOLVE, ROOT, SCALE, FREE. A destra PLAY battuta.tempo o STOP.'],
        ['3', 'Tracce', 'Numero e macchina: click = selezione; il quadratino accende/spegne il sequencer della traccia.'],
        ['4', 'Strisce di step', 'EUCLID: quadrato pieno = colpo, più alto = accento, barrato = muto a mano. GRID: i trig scritti, con la loro durata. '
                                'Sotto la posizione che suona; a destra rate, ultima nota e GRID se la traccia è in GRID.'],
        ['5', 'LANES', 'Le quattro lane di p-lock: manopola (click per scegliere), forma, finestra MIN–MAX col valore attuale, LEN e CH.'],
        ['6–8', 'Pagine traccia', '<b>RHYTHM</b> (STEP, PULS, ROT, RATE, ACNT, CHNC, COND, GATE), <b>MOTION</b> (RTCH, RMAX, SHAP, RPCH, SWNG, DRFT) e '
                                 '<b>PITCH</b> (MODE, SCAL, LOW, HIGH, LEN/JUMP, MUT, HOLD). In GRID, come qui sulla T4, al posto di PITCH c\'è <b>GRID STEP</b> e i controlli che non servono mostrano --.'],
        ['9', 'Collana', 'Il pattern come poligono, con la posizione che suona. SHAKE (un ritmo nuovo e musicale) e CLEAR (via i mute).'],
        ['10', 'Riga d\'aiuto', 'Spiega il controllo sotto il mouse.']]
story += [table(seqz, [9 * mm, 26 * mm, CW - 35 * mm], compact=True), Spacer(1, 3 * mm)]
gest = [['GESTO', 'EFFETTO'],
        ['Trascina su/giù o destra/sinistra', 'Cambia il valore. Con SHIFT premuto: passi fini.'],
        ['Rotella del mouse', 'Un passo alla volta.'],
        ['Click su una lista', 'Apre il menu dei valori (RATE, COND, MODE, SCALE, forme, target…).'],
        ['Doppio click', 'Torna al valore di default. Sulla finestra MIN–MAX: apre tutto (0–127).']]
story += [KeepTogether([P('Gesti sul display', h3), table(gest, [55 * mm, CW - 55 * mm], first_mono=False, compact=True)])]

story += [PageBreak(), SectionHead('04', "L'interfaccia", 'Scrivere le note: il modo GRID'),
          P("Ogni traccia può lavorare in due modi, scelti col tasto <b>GRID</b>. In <b>EUCLID</b> il sequencer genera ritmo e note. "
            "In <b>GRID</b> suoni gli step che scrivi tu, come su una groovebox Elektron: ogni trig ha la sua nota e la sua durata. "
            "Chance, condizioni, ratchet, swing, drift e lane continuano a valere anche in GRID. La prima volta che passi a GRID "
            "la traccia parte da una copia del ritmo euclideo, così hai già qualcosa da modificare."), Spacer(1, 3 * mm)]
grid = [['SUI TASTI TRIG', 'EFFETTO'],
        ['Click su un tasto vuoto', 'Nuovo trig con l\'ultima nota e durata usate; diventa lo step selezionato.'],
        ['Click su un trig', 'Lo seleziona (il nome della nota compare sopra il tasto).'],
        ['Click sul trig selezionato / tasto destro', 'Lo toglie.'],
        ['Trascina su / giù', 'Cambia la nota (con SHIFT: di un\'ottava).'],
        ['Trascina a destra', 'Cambia la durata, a quarti di step: anche più lunga dello step, legata ai successivi.'],
        ['Rotella', 'La nota, un semitono alla volta.']]
story += [table(grid, [58 * mm, CW - 58 * mm], first_mono=False, compact=True), Spacer(1, 3 * mm)]
kb = [['TASTIERA E REC', 'EFFETTO'],
      ['Un tasto', 'Suona la traccia e scrive quella nota nello step selezionato (lo crea se è vuoto).'],
      ['REC acceso, trasporto fermo', 'Ogni nota scritta fa avanzare allo step successivo: scrivi una frase nota per nota.'],
      ['REC acceso, trasporto in play', 'Le note che suoni (dalla tastiera o da una tastiera MIDI) vanno nello step che sta suonando.']]
story += [KeepTogether([table(kb, [58 * mm, CW - 58 * mm], first_mono=False, compact=True)]), Spacer(1, 3 * mm)]
gs = [['GRID STEP', 'COSA FA'],
      ['STEP / NOTE / LEN / TRIG', 'Lo step selezionato: numero, nota, durata (trascina) e trig acceso o spento (click).'],
      ['OCT', 'Click a sinistra / destra: tutto il pattern un\'ottava sotto / sopra.'],
      ['SHFT', 'Click a sinistra / destra: tutto il pattern uno step indietro / avanti.'],
      ['EUC> GRID', 'Copia il ritmo euclideo attuale (con le note dal range e dalla scala) negli step.'],
      ['CLR ALL', 'Toglie tutti i trig.']]
story += [KeepTogether([P('La pagina GRID STEP sul display', h3), table(gs, [40 * mm, CW - 40 * mm], compact=True)]), Spacer(1, 3 * mm),
          Callout('Note on e note off', "In GRID ogni trig manda il suo note on all'inizio dello step e il note off dopo la durata scritta. "
                  "Una durata più lunga dello step lega la nota con la successiva (sulle macchine mono diventa un legato). In EUCLID la "
                  "durata la decide GATE, in percentuale dello step o della ripetizione di ratchet.")]

# 05 how to
story += [PageBreak(), SectionHead('05', 'Come si usa', 'Dal play al primo pattern in otto passi'),
          steps_list([
              ('Carica il plugin su una traccia strumento', "Inserisci <b>Monomodule Euclid</b> come strumento. Nella pagina SYNTH seleziona "
               "il file OS se è la prima volta. Di default tutte e sei le tracce (e MAKU) escono dall'uscita stereo 1, quindi le senti subito; per separarle vai nella pagina MIX (capitolo 08) e scegli OUTPUTS = PER TRACK."),
              ('Premi play nella DAW', "Il sequencer parte con il trasporto, segue il tempo e i loop della DAW. Senza clip MIDI: le note nascono "
               "dentro il plugin. Per farlo girare a DAW ferma accendi <b>FREE</b>."),
              ('Scegli una traccia e il suo suono', "Premi un tasto TRACK (1–6). Nella pagina SYNTH cambia macchina e regola il timbro "
               "di base: è il valore a cui le manopole tornano tra un p-lock e l'altro."),
              ('Costruisci il ritmo', "In RHYTHM: " + k('STEP') + " lunghezza, " + k('PULS') + " quanti colpi, " + k('ROT') +
               " dove cadono, " + k('RATE') + " velocità. Prova lunghezze diverse su tracce diverse (16, 13, 11) e un rate dispari (1/20)."),
              ('Rendilo vivo', "Abbassa " + k('CHNC') + " (70–90%), aggiungi " + k('RTCH') + " al 15–35% con " + k('RMAX') +
               " x3–x6, un po' di " + k('DRFT') + ". Con " + k('COND') + " PRE/NEI le tracce si rispondono."),
              ('Dai le note', "In PITCH scegli " + k('MODE') + " e il range " + k('LOW') + "–" + k('HIGH') + ". FIXED per la batteria "
               "(una nota sola), WALK per i bassi, TURING per frasi che mutano, CYCLE per una melodia che gira contro il ritmo. "
               "Per i kit BBOX usa " + k('SCAL') + " CHROMATIC. Se vuoi una frase precisa premi " + k('GRID') + " e scrivi le note "
               "sui tasti trig (capitolo 04)."),
              ('Muovi i timbri con le lane', "In LANES scegli la manopola (per esempio FILT BASE o EFX DSND), stringi la finestra "
               "MIN–MAX e scegli la forma: RANDOM per il glitch, SINE per un'onda lenta, ACCENT per enfatizzare, PITCH per seguire la melodia."),
              ('Dai una forma al brano', "Con " + k('LOOP') + " a 2–8 battute il materiale si ripete e diventa riconoscibile; " + k('EVOLVE') +
               " al 5–15% lo fa mutare piano. " + k('DICE') + " cambia tutto. Automatizza CHANCE, EVOLVE o SEED nella DAW per gli arrangiamenti."),
          ]),
          Spacer(1, 5 * mm),
          Callout('Trasporre dal vivo', "Suona una nota sul <b>canale MIDI 16</b>: tutte le tracce melodiche si spostano di conseguenza "
                  "(C4 = nessuna trasposizione, la trasposizione resta finché non ne suoni un'altra). Le tracce in FIXED, cioè la batteria, non si muovono.")]

# 06 recipes
story += [PageBreak(), SectionHead('06', 'Ricette', 'Punti di partenza in stile Warp')]
rec = [
    ('Rimbalzi', 'Una percussione che cade come una pallina.',
     'BBOX, STEP 16, PULS 3, RATE 1/16 · RTCH 60%, RMAX x8, SHAP DECEL · lane 1: SYN PTCH, RAMP DN, 40–90.'),
    ('Polimetria a orologeria', 'Tre tracce che non si riallineano mai.',
     'T1 E(5,16) 1/16 · T2 E(4,11) 1/20 · T3 E(3,7) 1/28 · CHNC 85% ovunque · LOOP OFF.'),
    ('Basso che respira', 'Una linea che cammina e un filtro che si apre lentamente.',
     'SID, MODE WALK, LOW G1, HIGH D3, JUMP 2, HOLD 30% · lane 1: FILT BASE, SINE, LEN 48, 30–100.'),
    ('Frasi aliene', 'Melodie che si ripetono ma ogni tanto sbagliano.',
     'FM+ DYN, MODE TURING, LEN 7, MUT 10% · scala HIRAJOSHI · lane: SYN 1FEN RANDOM · LOOP 4 BARS, EVOLVE 12%.'),
    ('Accordi che scivolano', 'Un giro di accordi contro un ritmo lungo.',
     'SWAVE SAW, MODE CYCLE, LEN 5, scala MIN7 CHORD, RATE 1/8, STEP 32, PULS 5, GATE 90% · T6 REVERB su NEIBOR.'),
    ('Glitch di timbro', 'Ogni colpo è un suono diverso.',
     'Qualsiasi macchina · 4 lane RANDOM su SYN A, SYN C, EFX SRR, AMP DEC con CH 40–70% · finestre strette per restare musicali.'),
]
rows = []
for i in range(0, len(rec), 2):
    pair = []
    for t, d, r in rec[i:i + 2]:
        pair.append([Paragraph('<font name="Pop-B" size="11">%s</font>' % t, S('rt', leading=15)),
                     Paragraph(d, S('rd', fontSize=8.6, leading=12, textColor=GREY)), Spacer(1, 2 * mm),
                     Paragraph(r, S('rr', fontName='Mono', fontSize=7.2, leading=10.5, textColor=INK))])
    rows.append(pair)
rt = Table(rows, colWidths=[CW / 2 - 2 * mm, CW / 2 - 2 * mm], spaceBefore=2)
rt.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('BACKGROUND', (0, 0), (-1, -1), CARD),
                        ('LINEABOVE', (0, 0), (-1, -1), 2.2, LIME_D), ('LEFTPADDING', (0, 0), (-1, -1), 9),
                        ('RIGHTPADDING', (0, 0), (-1, -1), 9), ('TOPPADDING', (0, 0), (-1, -1), 8), ('BOTTOMPADDING', (0, 0), (-1, -1), 10),
                        ('LINEAFTER', (0, 0), (0, -1), 6, PAPER), ('LINEBELOW', (0, 0), (-1, -2), 6, PAPER)]))
story += [rt, Spacer(1, 5 * mm),
          Callout('Restare musicali', "Le tre leve che tengono il caso sotto controllo: un <b>range</b> stretto (un'ottava e mezza), "
                  "una <b>scala</b> con poche note (pentatoniche, accordi, HIRAJOSHI) e <b>finestre MIN–MAX</b> strette sulle lane. "
                  "Allarga una cosa alla volta.")]

# 07 reference
story += [PageBreak(), SectionHead('07', 'Riferimento parametri', 'Tutti automatizzabili dalla DAW'),
          P('Ogni parametro ha il suo nome nella DAW, per esempio <font name="Mono">SEQ3 PULSES</font> o <font name="Mono">SEQ1 LANE2 MAX</font>. '
            'Gli step silenziati a mano non sono parametri: vengono salvati con il progetto.', bodyS), Spacer(1, 3 * mm)]
ref_r = [['CONTROLLO', 'VALORI', 'FUNZIONE'],
         ['STEP', '1–32', 'Lunghezza del pattern.'],
         ['PULS', '0–32', 'Colpi distribuiti in modo euclideo sugli step.'],
         ['ROT', '0–31', 'Rotazione del pattern verso destra.'],
         ['RATE', '1/32 1/28 1/24 1/20 1/16 1/14 1/12 1/10 1/8 1/6 1/4 1/2', 'Durata dello step. 1/24, 1/12, 1/6 = terzine; 1/20, 1/10 = quintine; 1/28, 1/14 = settimine.'],
         ['ACNT', '0–32', 'Colpi del pattern di accento (stessi step e rotazione). Usato dalle lane in modo ACCENT.'],
         ['CHNC', '0–100%', 'Probabilità che un colpo suoni.'],
         ['COND', 'vedi tabella', 'Condizione del trig.'],
         ['GATE', '1–100%', 'Durata della nota rispetto allo step (o alla porzione di ratchet).']]
ref_m = [['CONTROLLO', 'VALORI', 'FUNZIONE'],
         ['RTCH', '0–100%', 'Probabilità che un colpo diventi un ratchet.'],
         ['RMAX', 'OFF, x2–x8', 'Numero massimo di ripetizioni (scelto a caso tra 2 e RMAX).'],
         ['SHAP', 'FLAT ACCEL DECEL RANDOM', 'Disposizione delle ripetizioni nello step.'],
         ['RPCH', '-4…+4', 'Gradi di scala aggiunti a ogni ripetizione (semitoni in FIXED).'],
         ['SWNG', '0–100%', 'Ritardo di uno step su due, fino a mezzo step.'],
         ['DRFT', '0–100%', 'Ritardo casuale di ogni colpo, fino a mezzo step.']]
ref_p = [['CONTROLLO', 'VALORI', 'FUNZIONE'],
         ['MODE', 'FIXED RANDOM WALK TURING CYCLE', 'Generatore delle note (vedi sotto).'],
         ['SCAL', 'GLOBAL + 22 scale', 'Scala della traccia; GLOBAL usa quella della barra in alto.'],
         ['LOW / HIGH', 'C-1…G9', 'Range delle note. In FIXED si usa solo LOW.'],
         ['LEN / JUMP', '1–32', 'TURING/CYCLE: lunghezza della frase. WALK: salto massimo in gradi.'],
         ['MUT', '0–100%', 'TURING: probabilità di invertire un bit. CYCLE: di sostituire una nota.'],
         ['HOLD', '0–100%', 'Probabilità di ripetere la nota precedente.']]
ref_l = [['CONTROLLO', 'VALORI', 'FUNZIONE'],
         ['TARGET', 'OFF, SYN A–H, AMP, FILT, EFX', 'La manopola da bloccare (32 destinazioni). Le SYN mostrano il nome della macchina.'],
         ['MIN / MAX', '0–127', 'Finestra dei valori. MIN > MAX inverte la forma.'],
         ['MODE', 'RANDOM CYCLE RAMP UP RAMP DN TRIANGLE SINE ACCENT PITCH TURING', 'Forma del movimento (vedi sotto).'],
         ['LEN', '1–64', 'Periodo in trig (SINE: in step).'],
         ['CH', '0–100%', 'Probabilità che il trig riceva il blocco; altrimenti la manopola torna al suo valore.']]
ref_g = [['CONTROLLO', 'VALORI', 'FUNZIONE'],
         ['SEED', '1–9999', 'Il seme di tutte le decisioni casuali. DICE ne sceglie uno a caso.'],
         ['LOOP', 'OFF, 1–32 BARS', 'Ripete la tessitura ogni N battute (segue il metro della DAW).'],
         ['EVOLVE', '0–100%', 'Probabilità che ogni step del loop muti a ogni giro.'],
         ['ROOT / SCALE', 'C…B, 22 scale', 'Tonica e scala globali.'],
         ['FREE', 'OFF / ON', 'Gira col proprio clock a DAW ferma (tempo: BPM della pagina SYNTH).']]
w3 = [24 * mm, 52 * mm, CW - 76 * mm]
for title, t in [('RHYTHM', ref_r), ('MOTION', ref_m), ('PITCH', ref_p), ('LANES', ref_l), ('GLOBALE', ref_g)]:
    story += [KeepTogether([P(title, h3), table(t, w3)]), Spacer(1, 3.5 * mm)]

cond = [['CONDIZIONE', 'IL COLPO SUONA SE…'],
        ['ALWAYS', 'sempre (resta valida CHANCE).'],
        ['X:Y', 'è il giro X di ogni Y giri del pattern (1:2, 2:2, 1:3 … 4:4).'],
        ['PRE / !PRE', 'il colpo precedente di questa traccia ha suonato / non ha suonato.'],
        ['NEI / !NEI', "l'ultimo colpo della traccia precedente (la T6 per la T1) ha suonato / non ha suonato."],
        ['1ST / !1ST', 'è il primo giro (del loop, o del pattern se LOOP è OFF) / non lo è.']]
modes = [['MODO', 'COME SCEGLIE LA NOTA'],
         ['FIXED', 'Sempre LOW, senza scala né trasposizione. Per batteria e BBOX.'],
         ['RANDOM', 'Una nota qualsiasi del range in scala.'],
         ['WALK', 'Passi vicini, al massimo JUMP gradi, rimbalzando ai bordi del range.'],
         ['TURING', 'Un registro a scorrimento di LEN bit: la frase si ripete e MUT ne cambia i bit.'],
         ['CYCLE', 'Una melodia di LEN note che si ripete, indipendente dal numero di step.']]
lmodes = [['FORMA', 'MOVIMENTO DELLA MANOPOLA'],
          ['RANDOM', 'Un valore nuovo a ogni trig.'], ['CYCLE', 'Una sequenza di LEN valori che si ripete.'],
          ['RAMP UP / DN', 'Rampa su LEN trig.'], ['TRIANGLE', 'Sale e scende su LEN trig.'],
          ['SINE', 'Onda lenta su LEN step (non trig): continua anche nei vuoti.'], ['ACCENT', 'MAX sugli accenti, MIN altrove.'],
          ['PITCH', 'Segue la posizione della nota nel range (key tracking).'], ['TURING', 'Segue il registro TURING della traccia.']]
story += [CondPageBreak(60 * mm), KeepTogether([P('Condizioni', h3), table(cond, [30 * mm, CW - 30 * mm])]), Spacer(1, 3.5 * mm),
          KeepTogether([P('Modi di pitch', h3), table(modes, [30 * mm, CW - 30 * mm])]), Spacer(1, 3.5 * mm),
          KeepTogether([P('Forme delle lane', h3), table(lmodes, [30 * mm, CW - 30 * mm])])]

# 08 maku
story += [PageBreak(), SectionHead('08', 'MAKU', 'Il generatore ambient dentro Euclid'),
          P("MAKU è una macchina ambient generativa di <b>okumurari</b> (derivata dal firmware Felucca di Leo Kuroshita), "
            "nata per la groovebox M-VAVE FM-1. Dentro Monomodule Euclid gira come <b>secondo motore</b>, accanto alle sei tracce "
            "Monomachine: le tracce euclidee portano ritmo e dettaglio, MAKU lo spazio e l'armonia sotto.", lead),
          Spacer(1, 3 * mm),
          P("MAKU non si programma: si <i>abita</i>. Ogni <b>mondo</b> sceglie a caso i suoni, la tonalità e dodici macro; tu lo "
            "fai crescere, affondare o rompere con dei gesti, e quando ti piace lo ricordi con un <b>LIKE</b>. Suona anche "
            "prima di aver scelto il file OS della Monomachine."),
          Spacer(1, 3 * mm)]
parts = [['PARTE', 'RUOLO', 'MACRO (ENCODER E–G)'],
         ['1 PULSE', 'La cassa. Con PULSE a fuoco i tasti bianchi della tastiera sono i suoi 16 step.', 'DENS (da un colpo a battuta al quattro quarti, poi hi-hat) · TONE · WASH'],
         ['2 FLOOR', 'Il drone: accordi lenti e aperti della scala.', 'OPEN (voci; sotto metà gli accordi si allungano) · TONE · SWAY'],
         ['3 HAZE', 'Lo shimmer granulare in alto.', 'GRAIN (densità; sotto metà si dirada) · TONE · AIR'],
         ['4 VOICE', "La frase arpeggiata. Le note che suoni ci entrano dentro e ci restano un po'.", 'LOOSE (quanto si scioglie e sale) · TONE · TRAIL']]
story += [table(parts, [22 * mm, 70 * mm, CW - 92 * mm], compact=True), Spacer(1, 4 * mm)]
st = [['IMPOSTAZIONE', 'VALORI', 'FUNZIONE'],
      ['CLOCK', 'HOST / FREE / HOST BPM', 'HOST: MAKU riceve il trasporto della DAW come clock MIDI, sulla stessa griglia del sequencer; parte sulla prima battuta dopo il play e si ferma con lo stop. Un loop o un salto non lo fa ripartire da capo. FREE: gira sempre, al suo tempo (70–90 BPM, diverso per ogni mondo). HOST BPM: gira sempre, al tempo della DAW, anche a trasporto fermo (non agganciato alla battuta).'],
      ['KEY', 'SEQ > MAKU / MAKU > SEQ / OFF', 'SEQ > MAKU: MAKU suona in ROOT e SCALE del sequencer. MAKU > SEQ: il sequencer prende la tonalità di ogni mondo. OFF: ognuno la sua.'],
      ['LEVEL', 'OFF, −60…+24 dB', 'Volume di MAKU (default +9 dB).'],
      ['MAKU ON', 'OFF / ON', 'Spento: silenzio e niente CPU; riacceso, riparte sulla battuta successiva.']]
story += [KeepTogether([P('Impostazioni (tutte automatizzabili)', h3), table(st, [22 * mm, 40 * mm, CW - 62 * mm], compact=True)]),
          Spacer(1, 4 * mm),
          Callout('Una tonalità, due motori', "FLAT e SHARP spostano la tonalità per quinte, come fa MAKU sul suo hardware: non è una "
                  "trasposizione ma un cambio di armatura, quindi cambia solo la nota che l'armatura sposta (da C a G: F diventa F#) e "
                  "le note che stanno suonando restano dove sono. Con KEY su SEQ > MAKU si sposta la ROOT del sequencer e MAKU la segue.",
                  dark=True)]


story += [PageBreak(), SectionHead('08', 'MAKU', 'La pagina MAKU e il pannello'),
          ShotM(os.path.join(IMG, 'maku-panel.png'), CW, 1994, 1040,
                [(1, 67, 640), (2, 160, 640), (3, 110, 215), (4, 485, 395), (5, 760, 128), (6, 1180, 360), (7, 1180, 612), (12, 905, 560),
                 (8, 1805, 215), (9, 1805, 455), (10, 110, 965), (11, 640, 960)]),
          Spacer(1, 4 * mm)]
mp = [['N', 'ZONA', 'COSA FA'],
      ['1', 'MAKU', 'Mostra MAKU sul display e trasforma il pannello nel suo; premuto di nuovo torna al SEQ.'],
      ['2', 'MAKU ON', 'Accende e spegne MAKU (spento non consuma CPU). Funziona da qualunque pagina.'],
      ['3', 'Tasti PART', 'I tasti traccia 1–4 diventano PULSE, FLOOR, HAZE, VOICE: scelgono la parte a fuoco (LED acceso).'],
      ['4', 'Schermo MAKU', "Il suo display originale a colori: onda, BPM, volumi, parte a fuoco; mentre tieni un gesto ne mostra i tempi."],
      ['5', 'Riga di stato', 'MAKU ON, CLOCK, KEY, LEVEL (click o trascina) e a destra PLAY / WAIT FOR PLAY con i BPM.'],
      ['6', 'PARTS', 'Ogni parte: nome, suono del mondo, LEVEL e i tre macro. Trascina o usa la rotella; click sul nome = fuoco.'],
      ['7', 'Bottoni', 'NEW WORLD, LIKE (tasto destro: dimentica tutti i like), FLAT e SHARP.'],
      ['8', 'Encoder', 'A–D volumi delle quattro parti, E–G i macro della parte a fuoco, H il volume di MAKU.'],
      ['9', 'VERBS', 'I tasti pagina diventano i gesti di MAKU (sotto).'],
      ['10', 'FLAT / SHARP', 'La tonalità comune scende / sale di una quinta.'],
      ['11', 'Tastiera', 'Suona la parte a fuoco; le note entrano nella frase di VOICE. Con PULSE: i 16 step della cassa.'],
      ['12', 'Collegamento', 'CHORD, VERBS>SEQ e FEED (pagina seguente); a destra i sei gesti attivi in questo momento. Sotto lo schermo: l\'accordo di FLOOR.']]
story += [table(mp, [9 * mm, 26 * mm, CW - 35 * mm], compact=True)]

story += [PageBreak(), SectionHead('08', 'MAKU', 'I due motori si ascoltano'),
          P("Sequencer e MAKU non suonano soltanto insieme: si influenzano. MAKU porta al sequencer la sua armonia e i suoi "
            "gesti, il sequencer porta a MAKU le sue note. Tre impostazioni, nella riga sopra i bottoni della pagina MAKU, "
            "decidono quanto."), Spacer(1, 3 * mm)]
lk = [['IMPOSTAZIONE', 'VALORI', 'COSA FA'],
      ['CHORD', '0–100% (40%)', "Quella percentuale delle note melodiche delle tracce EUCLID (non FIXED, non GRID) si sposta sulla nota più vicina dell'accordo che FLOOR sta suonando, restando nel range LOW–HIGH. Le melodie euclidee seguono così il drone, accordo dopo accordo."],
      ['VERBS > SEQ', '0–100% (70%)', 'Quanto i gesti di MAKU muovono anche il sequencer (tabella sotto). A 0% agiscono solo su MAKU.'],
      ['FEED', 'OFF, T1–T6', 'Le note di quella traccia euclidea entrano nella frase di VOICE: MAKU le ascolta e le riprende ogni tanto nel suo arpeggio, poi le dimentica piano.']]
story += [table(lk, [24 * mm, 26 * mm, CW - 50 * mm], compact=True), Spacer(1, 4 * mm)]
vs = [['GESTO', 'SU MAKU', 'SUL SEQUENCER (A VERBS > SEQ 100%)'],
      ['SINK', 'affonda, la cassa sparisce', 'colpi diradati: batteria (FIXED) fino al 90%, melodie fino al 50%'],
      ['CASCADE', "pioggia di note dell'arpeggio", 'ratchet in salita sulle tracce melodiche (+60%, fino a x5, +1 grado a ripetizione)'],
      ['SWELL', 'accordi lunghi che si gonfiano', 'GATE verso il legato'],
      ['WOBBLE', 'filtri e intonazione ondeggiano', 'più DRIFT: il tempo dei colpi sbanda (+40%)'],
      ['LIFT', 'tutto più brillante', "metà delle note melodiche un'ottava sopra"],
      ['SCRAMBLE', 'arpeggio e cassa si rompono', 'colpi saltati, ratchet casuali, note a caso nel range, più drift']]
story += [KeepTogether([P('I gesti sul sequencer', h3), table(vs, [24 * mm, 52 * mm, CW - 76 * mm], compact=True)]), Spacer(1, 2 * mm),
          P("I gesti seguono il loro inviluppo (sale, resta, torna piano), quindi anche il sequencer torna com'era da solo. Le decisioni "
            "restano legate al SEED: stessa posizione e stesso gesto, stesso risultato.", bodyS), Spacer(1, 4 * mm)]
daw = [['DALLA DAW', 'MESSAGGIO', 'EFFETTO'],
       ['Canali 11–14', 'Note On / Off', 'Suonano PULSE, FLOOR, HAZE, VOICE.'],
       ['Canale 15', 'Note On / Off', "Suona la parte a fuoco; le note entrano nella frase di VOICE, come dalla tastiera."],
       ['Canale 15', 'CC 102–107', 'Tengono premuti SINK, CASCADE, SWELL, WOBBLE, LIFT, SCRAMBLE (valore 64 o più = premuto).'],
       ['Canale 15', 'CC 108 / 109 / 110', 'Il tasto DRIFT (premi e rilascia = acceso/spento), NEW WORLD e LIKE (al passaggio sopra 64).']]
story += [KeepTogether([P('Suonare MAKU dalla DAW', h3), table(daw, [24 * mm, 30 * mm, CW - 54 * mm], compact=True)]), Spacer(1, 2 * mm),
          P("Così i gesti si possono scrivere in una clip o automatizzare: un CC 102 a 127 per due battute è un SINK tenuto per due battute.", bodyS)]

story += [PageBreak(), SectionHead('08', 'MAKU', 'La pagina MIX e il routing tra i motori'),
          ShotM(os.path.join(IMG, 'mix-crop.png'), CW, 1750, 655,
                [(2, 410, 33), (3, 470, 265), (4, 985, 265), (5, 1230, 205), (6, 1230, 385), (7, 1575, 120), (8, 1575, 360)]),
          Spacer(1, 4 * mm)]
mx = [['N', 'ZONA', 'COSA FA'],
      ['1', 'MIX', 'Il tasto sotto MAKU / MAKU ON, a sinistra: mostra il mixer sul display; premuto di nuovo torna al SEQ.'],
      ['2', 'OUTPUTS', "STEREO MIX (default): le sei tracce e MAKU sull'uscita 1, si sente tutto subito. PER TRACK: ogni traccia sulla sua uscita, MAKU sulla settima. BUSES: i bus AB, CD, EF sulle uscite 1–3."],
      ['3', 'Canali T1–T6', 'Meter, fader LEVEL (trascina, rotella, doppio click = 100) e MUTE.'],
      ['4', 'Canale MAKU', 'Meter, fader LEVEL in dB e ON/OFF.'],
      ['5', 'ROUTING', 'SEND e LVL, DUCK, SIDE e DEPTH, DRIFT (sotto).'],
      ['6', 'Indicatori', 'Quanto stanno abbassando adesso DUCK e SIDECHAIN; se DRIFT è acceso in MAKU.'],
      ['7', 'Encoder', 'A–F i livelli delle tracce, G il livello di MAKU, H il SEND.'],
      ['8', 'MUTES', 'I tasti pagina diventano MUTE 1–6 e MAKU ON (LED acceso = muto / acceso).']]
story += [table(mx, [9 * mm, 26 * mm, CW - 35 * mm], compact=True), Spacer(1, 4 * mm)]
rt = [['IMPOSTAZIONE', 'VALORI', 'COSA FA'],
      ['SEND', 'OFF, BUS AB/CD/EF', "MAKU entra anche in un bus di mix. Una traccia FX (REVERB, CHORUS, RINGMOD, PHASER…) con ingresso BUS AB/CD/EF lo elabora: MAKU passa nei filtri e negli effetti della Monomachine."],
      ['LVL', '0–100% (50%)', 'Quanto MAKU va nel bus.'],
      ['DUCK', '0–100% (0%)', 'La cassa di MAKU abbassa le sei tracce, come già abbassa i suoi droni: tutto respira con PULSE (da DENSITY 40 in su).'],
      ['SIDE', 'OFF, T1–T6', 'Quella traccia abbassa MAKU a ogni colpo: per esempio la cassa euclidea su T1 che scava spazio nel drone.'],
      ['DEPTH', '0–100% (60%)', 'Quanto scende MAKU sotto la sidechain.'],
      ['DRIFT', '0–100% (50%)', "Con DRIFT acceso in MAKU anche i pattern euclidei vagano piano: PULSES ±1–2, ROTATE ±3 e un po' meno CHANCE, un posto nuovo ogni 4 battute. Sempre dal SEED: stessa posizione, stesso risultato."]]
story += [KeepTogether([P('Il routing tra i due motori', h3), table(rt, [24 * mm, 32 * mm, CW - 56 * mm], compact=True)])]

story += [PageBreak(), SectionHead('08', 'MAKU', 'I gesti e come si usa'),
          P("I primi sei tasti VERBS sono <b>gesti</b>: tienili premuti, il suono cambia subito e, quando lasci, torna piano "
            "da solo. Si possono sovrapporre. Funzionano a coppie opposte: SINK/LIFT (indietro/avanti), SWELL/SCRAMBLE "
            "(lungo/frammentato), CASCADE/WOBBLE (più note/più movimento)."), Spacer(1, 2 * mm)]
vb = [['TASTO', 'COSA SUCCEDE'],
      ['SINK', 'La cassa sparisce, riverbero e delay si aprono e il suono affonda; al ritorno la cassa rientra in dissolvenza.'],
      ['CASCADE', "L'arpeggio suona su ogni step, con ottave e terze sopra: una pioggia di note."],
      ['SWELL', "Drone e shimmer riattaccano l'accordo, attacchi e code si allungano, l'accordo resta."],
      ['WOBBLE', "Filtri e chorus ondeggiano veloci, l'intonazione oscilla."],
      ['LIFT', 'Tutti i TONE salgono al massimo, più drive, il passa-alto sale: in avanti e luminoso.'],
      ['SCRAMBLE', 'Arpeggio e cassa si rompono: salti, ripetizioni, rollate, colpi che saltano.'],
      ['DRIFT', 'Un tocco lo accende o lo spegne: i 12 macro camminano da soli a piccoli passi e ogni 40–60 s un timbro cambia di nascosto.'],
      ['NEW WORLD', 'Un mondo nuovo. Con dei like salvati, spesso ne riprende uno e lo varia un po\'.'],
      ['LIKE', 'Ricorda il mondo che suona (fino a 16). Più like dai, più i nuovi mondi somigliano ai tuoi gusti.']]
story += [table(vb, [26 * mm, CW - 26 * mm], compact=True)]
story += [Spacer(1, 5 * mm), P('Come si usa', h2), Spacer(1, 1 * mm),
          steps_list([
              ('Premi play', "Con CLOCK su HOST, MAKU aspetta il play della DAW (in alto a destra: WAIT FOR PLAY) e parte sulla "
               "prima battuta. Per sentirlo subito, a DAW ferma, metti CLOCK su FREE."),
              ('Scegli la tonalità', "Imposta ROOT e SCALE del sequencer: con KEY su SEQ > MAKU vale per entrambi. Le scale che MAKU non ha "
               "diventano la sua più vicina (per esempio HIRAJOSHI diventa la minore naturale)."),
              ('Bilancia le parti', "Premi " + k('MAKU') + ": gli encoder A–D sono i volumi di PULSE, FLOOR, HAZE e VOICE. Abbassa "
               "PULSE se la cassa la fanno le tracce euclidee."),
              ('Dai forma a una parte', "Scegli la parte con i tasti PART e gira E–G: verso destra ogni macro è più intenso, verso "
               "sinistra si dirada e si allunga."),
              ('Suona i gesti', "Tieni " + k('SWELL') + " sull'attacco di una sezione, " + k('SINK') + " per un break, " + k('SCRAMBLE') +
               " per un fill. Accendi " + k('DRIFT') + " per lasciarlo muovere da solo per minuti."),
              ('Cambia mondo e ricorda', k('NEW WORLD') + " finché non trovi il mondo giusto, poi " + k('LIKE') + ". Il mondo che suona "
               "e i like vengono salvati con il progetto della DAW."),
              ('Mixa e separa', "Di default tutto esce dall'uscita 1. Nella pagina " + k('MIX') + " regoli i livelli e, con OUTPUTS = PER TRACK, "
               "dai a ogni traccia la sua uscita e a MAKU la sua (\"MAKU\", dopo Track 1–6) per gli effetti della DAW. Con DUCK e SIDE i due motori "
               "respirano uno sull'altro."),
          ]),
          Spacer(1, 4 * mm),
          Callout('Licenza', "MAKU è software libero (GPL-3.0-only) di okumurari, basato su Felucca di Leo Kuroshita (Hügelton "
                  "Instruments). Il suo codice e le sue licenze sono nel progetto, in ext/maku, e nella cartella MAKU dello zip.")]

# 09 midi
story += [PageBreak(), SectionHead('09', 'Implementazione MIDI', 'Ingressi, CC, sysex, sincronia'),
          P("Monomodule Euclid riceve MIDI e non ne trasmette. Il sequencer genera i suoi eventi dentro il plugin e li "
            "mescola con quelli in arrivo dalla DAW, ordinati al campione."), Spacer(1, 3 * mm)]
mi = [['MESSAGGIO', 'CANALE', 'EFFETTO'],
      ['Note On / Off', '1–6', 'Suona la traccia 1–6 (mono, priorità all\'ultima nota). La velocity è ignorata.'],
      ['Note On', '16', 'Trasposizione delle tracce melodiche: nota − 60 semitoni, trattenuta. Non suona.'],
      ['CC 123 / 120', '1–6', 'All notes off / all sound off sulla traccia.'],
      ['Control Change', '1–6', 'Mappa CC dell\'hardware (sotto): cambia il valore base della manopola, come un movimento in DAW.'],
      ['Note On / Off', '11–14', 'MAKU: suonano PULSE, FLOOR, HAZE, VOICE.'],
      ['Note / CC', '15', 'MAKU: la parte a fuoco (le note entrano in VOICE); CC 102–107 gesti, 108 DRIFT, 109 NEW WORLD, 110 LIKE.']]
story += [table(mi, [30 * mm, 18 * mm, CW - 48 * mm], compact=True), Spacer(1, 3 * mm)]
cc = [['CC', 'DESTINAZIONE', 'CC', 'DESTINAZIONE'],
      ['7', 'LEVEL', '80–87', 'EFX 1–8 (EQF … DWID)'],
      ['48–55', 'SYN A–H (dipende dalla macchina)', '88–95', 'LFO 1: PAGE … DPTH'],
      ['56–63', 'AMP 1–8 (ATK … PORT)', '104–111', 'LFO 2: PAGE … DPTH'],
      ['72–79', 'FILT 1–8 (BASE … WOFS)', '112–119', 'LFO 3: PAGE … DPTH']]
cct = table(cc, [18 * mm, CW / 2 - 18 * mm, 18 * mm, CW / 2 - 18 * mm])
story += [KeepTogether([P('Mappa CC (identica alla Monomachine)', h3), cct]), Spacer(1, 3 * mm)]
sx = [['SYSEX', 'BYTE', 'SIGNIFICATO'],
      ['Clear lock', 'F0 7D 01 tt F7', 'Toglie tutti i p-lock della traccia tt (0–5).'],
      ['Set lock', 'F0 7D 02 tt kk vv F7', 'Blocca la manopola kk della traccia tt al valore vv (0–127) fino al prossimo clear. '
                                            'kk = pagina × 8 + manopola: 0–7 SYN, 8–15 AMP, 16–23 FILT, 24–31 EFX.']]
story += [KeepTogether([P('P-lock via sysex', h3),
                        P('È il formato con cui il sequencer blocca le manopole (ID 7D = uso non commerciale). Puoi inviarlo anche dalla DAW o da un altro sequencer: il plugin lo accetta in ingresso.', bodyS),
                        Spacer(1, 2 * mm), table(sx, [22 * mm, 38 * mm, CW - 60 * mm], compact=True)]), Spacer(1, 3 * mm)]
sy = [['SINCRONIA', 'COMPORTAMENTO'],
      ['Play / Stop', 'Il sequencer segue il trasporto. Allo stop: all notes off e p-lock azzerati.'],
      ['Posizione', 'Gli step sono calcolati dalla posizione in quarti (PPQ) della DAW: allineamento al campione, anche con cambi di tempo.'],
      ['Loop / salti', 'Un salto di posizione riparte pulito dal punto nuovo; l\'evoluzione del LOOP viene conservata.'],
      ['Metro', 'LOOP usa la lunghezza della battuta della DAW (es. 7/8).'],
      ['Senza DAW', 'Standalone o FREE attivo: clock interno al BPM della pagina SYNTH.'],
      ['MAKU', 'CLOCK HOST: il trasporto della DAW arriva a MAKU come clock MIDI (24 impulsi al quarto, START sulla battuta, STOP). Con HOST BPM gira al tempo della DAW anche a trasporto fermo.']]
story += [KeepTogether([P('Sincronia', h3), table(sy, [26 * mm, CW - 26 * mm], compact=True)]), Spacer(1, 2 * mm),
          P('Monomodule Euclid deriva da Monomodule di Shnolk (GNU AGPL v3), che usa dsp56300 e JUCE, e include MAKU di okumurari (GPL-3.0-only). Elektron e Monomachine sono marchi di '
            'Elektron Music Machines MAQ AB; il plugin non è affiliato a Elektron e non contiene software Elektron.', monoG)]

story += [NextPageTemplate('egg'), PageBreak(), Spacer(1, 1)]

doc = Doc(os.path.join(HERE, '..', 'Monomodule-Euclid-Manuale.pdf'), pagesize=A4, leftMargin=ML, rightMargin=MR, topMargin=MT, bottomMargin=MB,
          title='Monomodule Euclid · Manuale utente', author='Monomodule Euclid', subject='Monomachine emulata e sequencer euclideo generativo')
fr = Frame(ML, MB, CW, H - MT - MB, id='f', leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
full = Frame(0, 0, W, H, id='full')
doc.addPageTemplates([PageTemplate('cover', [full], onPage=cover), PageTemplate('body', [fr], onPage=header_footer, onPageEnd=header_text),
                      PageTemplate('egg', [full], onPage=easter)])
doc.build(story)
print('ok')
