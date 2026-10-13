# Monomodule

**Monomodule** is a free, open-source, chip-level emulation of the Elektron Monomachine (SFX-6 / SFX-60)
as plugins for your DAW. Monomodule doesn't approximate the synth with new DSP code. It runs the
Monomachine's own sound engine inside an emulated Motorola DSP56300, so it matches the hardware sample for sample.

> **Monomodule is not affiliated with, endorsed by or sponsored by Elektron.** "Elektron" and "Monomachine" are
> trademarks of Elektron Music Machines MAQ AB, used here only to describe what Monomodule emulates. Monomodule
> contains no Elektron software. To make sound it needs the Monomachine OS file, which you download from Elektron
> yourself (see [Getting the OS file](#getting-the-os-file)).

## What's included

| | Type | What it does |
|---|---|---|
| **Monomodule One** | Instrument (AU, VST3, standalone) | One Monomachine track. Plays all 22 machines: GND, SWAVE, SID, DPRO, FM+, VO-6 and the FX machines. The editor recreates the hardware's LCD pages, with the SYN, AMP, FILT and EFX pages plus three LFOs. FX machines process the side-chain input. |
| **Monomodule Six** | Instrument (AU, VST3, standalone) | All six tracks in one instance. MIDI channels 1-6 play tracks 1-6. It has six stereo outputs, and each track has its own routing: OUT BUS AB/CD/EF, and for FX machines an input of NEIBOR, INP A/B/AB or BUS AB/CD/EF. |
| **Monomodule FX** | Effect (AU, VST3) | The FX machines as an audio effect: THRU, REVERB, CHORUS, DYNAMIX, RINGMOD, PHASER and FLANGER. It processes its input continuously, so the controls that only respond to trigs on the hardware are hidden and held open: the AMP envelope (ATK, HOLD, DEC, REL), portamento and the filter envelope. Presets keep their values, and One and Six still use them. |
| **Monomodule Euclid** | Instrument (AU, VST3, standalone) | Six with a generative Euclidean sequencer that plays its six tracks in sync with the host. See [Monomodule Euclid](#monomodule-euclid). |
| **Monomodule Library** | Standalone app | A library for Monomachine sysex dumps. It imports `.syx` files without losing any data, keeps a version history for each Monomachine you own, and lets you browse presets, kits and patterns with audio previews. The previews play through the system's audio output and follow it when you switch it; **Audio Output** in the menu selects another device. You can drag items into the plugins or your DAW. |

Every parameter is a raw 0-127 value, just like on the hardware, so a track's settings are the same as a kit track
in a sysex dump. The plugins and the Library app share presets, kits and patterns.

Every window can be resized from 50% to 200% with its proportions kept: drag the grip in a plugin's bottom-right
corner, or the edges of the Library window. All the plugins and the Library app share one size setting: a new window
opens at the size you last chose in any of them, or smaller on a screen it would not fit.

## Getting the OS file

Monomodule needs **Monomachine OS 1.32B**, which Elektron provides as a free download:

1. Download [Elektron_SFX6-60_OS1.32B.zip](https://www.elektron.se/wp-content/uploads/2024/09/Elektron_SFX6-60_OS1.32B.zip)
   from Elektron's website.
2. Unzip it. Inside is `Elektron_SFX6-60_OS1.32B.syx`.
3. Open any Monomodule plugin (or the Library app), click **Select OS File...** and choose that `.syx` file.

You only have to do this once. All Monomodule plugins and the Library app share the setting. The file stays where
you put it: Monomodule reads it each time it starts and never copies it or redistributes it.

## Installing a release

Pre-built downloads for macOS, Windows and Linux are on the project's **Releases** page.

| Platform | Plugins | Library app |
|---|---|---|
| macOS 12+ (Apple Silicon and Intel) | Run the installer. It installs the AU and VST3 plugins in `/Library/Audio/Plug-Ins/`. | `/Applications/Monomodule Library.app` |
| Windows 10+ (x64) | Run the installer. It installs the VST3 plugins in `C:\Program Files\Common Files\VST3\`. Or use the zip and copy the `.vst3` folders there yourself. | `C:\Program Files\Shnolk\Monomodule\Monomodule Library.exe` |
| Linux (x64) | Copy the `.vst3` folders to `~/.vst3/` | `~/.local/bin/Monomodule Library` |

The plugin's library panel opens the Library app from these locations.

## Monomodule Euclid

Monomodule Six plus a generative sequencer, in one plugin: a **SEQUENCER** tab beside the usual pages (**SYNTH**).
It plays the six tracks with notes and **p-locks** (a lock holds a knob at a value until the track's next trig, as
on the hardware), locked to the host transport sample by sample. Every setting is a host parameter, so it can be
automated. Built for evolving, polymetric, glitchy textures in the spirit of the Warp catalogue, but every note stays
inside a range and a scale, so it keeps sounding musical.

Per track:

* **RHYTHM**: a Euclidean pattern (STEP 1-32, PULS, ROT) at its own **RATE**: straight, triplet and odd tuplets
  (1/20, 1/28, 1/14, 1/10), so tracks drift against each other. **ACNT** is a second Euclidean pattern on the same
  steps (accents). **CHNC** is the trig probability, **COND** an Elektron-style trig condition (1:2 ... 4:4, PRE, NEI,
  1ST and their negations), **GATE** the note length. Click a step in the strip to mute it by hand.
* **MOTION**: **RTCH** ratchet chance, **RMAX** up to 8 repeats inside the step, **SHAP** FLAT / ACCEL / DECEL
  (bouncing ball) / RANDOM, **RPCH** scale degrees added per repeat; **SWNG** and **DRFT** (random timing delay).
* **PITCH**: notes always fall in **LOW-HIGH** and in the **SCALE** (the track's own, or the global one). **MODE**:
  FIXED (drums: always LOW), RANDOM, WALK (brownian, JUMP = max step), TURING (a shift register that mutates with
  MUT), CYCLE (a melody of LEN notes that phases against the rhythm). **HOLD** repeats the previous note.
* **LANES**: four p-lock lanes. Each targets any SYN, AMP, FILT or EFX knob of the track's machine and moves it inside
  a MIN-MAX window on every trig: RANDOM, CYCLE, RAMP UP/DN, TRIANGLE, SINE, ACCENT, PITCH (follows the note), TURING;
  **CH** is the chance that a trig gets the lock.

Global (tab bar): **SEED** (DICE picks a new one), **LOOP** (OFF, or the texture repeats every 1-32 bars),
**EVOLVE** (each step of the loop mutates with this chance every time round), **ROOT**, **SCALE**, and **FREE** (run on
the plugin's own clock while the host is stopped; the standalone app always does). Notes on **MIDI channel 16**
transpose every melodic track (latched; C4 = no transposition). Live MIDI on channels 1-6 still plays the tracks.

Every random decision is a hash of the seed and the song position, so the same position always plays the same thing:
a bounce sounds like the playback. The default kit is a ready texture: DPRO BBOX kick and hits, FM+ DYN, a SID bass,
SWAVE chords and a REVERB on track 6 (fed by track 5).

### MAKU: the ambient generator inside Euclid

Monomodule Euclid also carries **MAKU** ([okumurari2/maku](https://github.com/okumurari2/maku), by okumurari, after
Leo Kuroshita's Felucca), a generative ambient machine written as firmware for the M-VAVE FM-1, here running as its
own engine beside the six Monomachine tracks: a kick (**PULSE**), a drone (**FLOOR**), a granular shimmer (**HAZE**)
and an arpeggiated phrase (**VOICE**), with a new random "world" (sounds, key, macros) at every **NEW WORLD**. The
Euclidean tracks carry the rhythm and the detail, MAKU the space and the harmony underneath.

* **In time with the DAW**: CLOCK **HOST** sends MAKU the host transport as MIDI clock, on the same grid as the
  sequencer; it starts on the first bar line after you press play and stops with the transport. **FREE** lets it run
  on its own tempo.
* **One key for both**: KEY **SEQ > MAKU** (default) plays MAKU in the sequencer's ROOT and SCALE; **MAKU > SEQ** makes
  the sequencer follow each world's key; **OFF** keeps them apart. **FLAT / SHARP** move the shared key round the circle
  of fifths, as MAKU does on its hardware (only the note the key signature changes moves).
* **The panel plays it**: the **MAKU** key puts MAKU on the LCD (its own colour screen beside the four parts, their
  LEVEL and three macros each) and turns the panel into MAKU's: TRACK keys 1-4 focus a part, encoders A-D are the
  part levels, E-G the focused part's macros, H MAKU's level, and the page keys become its **verbs**, held like
  performance keys: **SINK**, **CASCADE**, **SWELL**, **WOBBLE**, **LIFT**, **SCRAMBLE**, plus **DRIFT** (the macros
  wander on their own), **NEW WORLD** and **LIKE** (a liked world shapes the next new worlds; the likes and the world
  playing are saved with the session). The keyboard plays the focused part; with PULSE in focus its white keys are the
  kick's 16 steps.
* **Its own output**: MAKU has a stereo bus of its own (**MAKU**, after Track 1-6), folded into the main output when
  the host leaves it off; **MAKU ON** switches it off entirely (no CPU).

MAKU plays even before the Monomachine OS file is chosen. Its source is in [`ext/maku`](ext/maku/README.md)
(GPL-3.0-only), built into a library that each plugin instance loads a private copy of.

## Building from source

### Requirements

- CMake 3.22 or newer, Git, and a C++17 compiler
- An internet connection for the first configure. CMake downloads [JUCE](https://github.com/juce-framework/JUCE)
  8.0.9 and [dsp56300](https://github.com/dsp56300/dsp56300), pinned to a fixed commit, and applies
  `ext/patches/0001-dsp56300-mnm.patch` to it.

### macOS

Needs Xcode or the Xcode command line tools (`xcode-select --install`). [Ninja](https://ninja-build.org) is optional
but faster.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The build copies the AU and VST3 plugins to `~/Library/Audio/Plug-Ins/`. For a universal (Apple Silicon and
Intel) build, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the first command.

### Linux

On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake git ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev libcurl4-openssl-dev libgtk-3-dev libwebkit2gtk-4.1-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The emulator keeps its memory in `/dev/shm`. On a normal desktop that folder is large enough, but a Docker container gets
only 64 MB by default, which crashes the engine. In a container, start it with `--shm-size=1g` or more.

The build copies the VST3 plugins to `~/.vst3/`. The standalone versions of One and Six and the Library app are in
`build/src/plugin/*_artefacts/Release/Standalone/` and `build/src/app/MnmLibraryApp_artefacts/Release/`.

### Windows

Needs Visual Studio 2022 with the **Desktop development with C++** workload, and for MAKU in Monomodule Euclid a
MinGW-w64 `gcc` (for instance from [winlibs](https://winlibs.com) or MSYS2) on `PATH` or in `C:\mingw64\bin`: MSVC does
not compile MAKU's GNU C. Without it Euclid is built without MAKU. Run these in a *Developer PowerShell for VS 2022*:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 plugins are in `build\src\plugin\*_artefacts\Release\VST3\`. Copy them to
`C:\Program Files\Common Files\VST3\`, or configure with `-DMNM_COPY_PLUGINS=ON` from an administrator prompt so the
build copies them for you.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `MNM_BUILD_PLUGIN` | `ON` | Build the plugins and the Library app. `OFF` builds only the engine, the tests and `mnm-render`. |
| `MNM_BUILD_TESTS` | `ON` | Build the unit tests. |
| `MNM_COPY_PLUGINS` | `ON` (`OFF` on Windows) | Copy the plugins to the plugin folders after each build. |
| `MNM_OS_SYX` | *(empty)* | Path to your OS file. Used by the tests and the command-line tools. |
| `MNM_DSP56300_DIR` | *(empty)* | Use an existing, already patched dsp56300 checkout instead of downloading one. |
| `MNM_MAKU` | `ON` | Build MAKU into Monomodule Euclid (needs a GNU C compiler: gcc or clang; MinGW-w64 gcc on Windows). |
| `MNM_MAKU_CC` | *(empty)* | The C compiler for the MAKU library. Default: the project's C compiler; with MSVC, MinGW-w64 `gcc` from `PATH` or `C:\mingw64\bin`. |

### Tests

```bash
cmake -S . -B build -DMNM_OS_SYX=/path/to/Elektron_SFX6-60_OS1.32B.syx
cmake --build build --target mnm-tests
ctest --test-dir build --output-on-failure
```

The tests that run the emulated DSP need the OS file. You can pass it through `MNM_OS_SYX` as above, or through the
`MNM_OS` environment variable. Without it, those tests are reported as skipped and the rest still run.

### Command-line tools

- `mnm-render` plays one note on one machine and writes a WAV file, e.g.
  `mnm-render --fw OS.syx --machine "FM+ PAR" --note 69 --dur 1 --out a4.wav`. Run it without arguments to see every
  option.
- `mnm-libtool` is the Library from the terminal: `import`, `list`, `show`, `export`, `verify` (a byte-for-byte
  round trip of a dump), `preview` (renders an audio preview to a WAV file) and more.

## How it works

- **OS file.** `src/core/firmware` unpacks the user's OS file: the sysex transport, the flash container and the
  compressed sections. From these it takes the DSP program and data.
- **DSP.** `src/core/dsp` loads that program into the dsp56300 emulator. Its JIT compiles the DSP code to native
  code. A small harness replaces the hardware's audio I/O: every 16 samples it sends in a 52-word parameter block
  for one track and reads out 16 stereo frames.
- **Host model.** On the hardware, the main processor builds those parameter blocks. `src/core/host` does the same
  job: note and trigger handling, tempo, parameter smoothing and the three LFOs of each track.
- **Plugins.** `src/plugin` holds the plugins. One, Six and FX share one processor and one editor. The editor draws
  the LCD with the fonts and icons from your OS file. Without an OS file it falls back to a built-in stand-in font.
- **Library.** `src/core/library` and `src/library` hold the sysex codec, the catalogue that merges identical items
  across dumps, the project and version store, and offline preview rendering. The app is in `src/app`.

## Known limitations

- DPRO-DDRW and DPRO-DENS read the Digibank waveforms, which live in the Monomachine's +Drive memory and are not
  part of the OS file. Monomodule builds a stand-in bank instead: the 32 DPRO-WAVE waveforms plus 32 classic
  single-cycle shapes.
- The delay time of the EFX page does not yet follow DTIM and the tempo the way the hardware does.
- A machine change triggers the machine once, as on the hardware. Because of this, a new instance plays a short
  blip right after the OS loads.
- MIDI transfer to and from the hardware isn't implemented yet. To move dumps, use `.syx` files with Elektron's C6
  or Transfer tools.

## Settings and data

Monomodule stores its settings and its library in the user application-data folder:

- macOS: `~/Library/Application Support/Shnolk/Monomachine/`
- Windows: `%APPDATA%\Shnolk\Monomachine\`
- Linux: `~/.config/Shnolk/Monomachine/`

## Licence

Monomodule is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)). Anyone who distributes Monomodule, modified or
not, must pass on these same rights and make the corresponding source code available.

It is built on [JUCE](https://github.com/juce-framework/JUCE) (AGPLv3) and
[dsp56300](https://github.com/dsp56300/dsp56300) (GPLv3), among others. Monomodule Euclid includes
[MAKU](https://github.com/okumurari2/maku) (GPL-3.0-only, source and licences in [ext/maku](ext/maku/README.md)).
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every dependency, its licence and the changes Monomodule makes to
dsp56300.

## Credits

Monomodule is made by **Shnolk**. Contact: [@shnolk](https://www.instagram.com/shnolk) on Instagram, or
[shnolk@halftone.world](mailto:shnolk@halftone.world).

Monomodule stands on the work of:

- [dsp56300](https://github.com/dsp56300/dsp56300), the DSP56300 emulator at the heart of the sound engine
- [JUCE](https://github.com/juce-framework/JUCE), the plugin and UI framework
- [MAKU](https://github.com/okumurari2/maku) by okumurari and [Felucca](https://github.com/hugelton/Felucca) by Leo
  Kuroshita (Hügelton Instruments), the ambient generator inside Monomodule Euclid
- [AsmJit](https://github.com/asmjit/asmjit), the JIT back end of dsp56300
- [MCL](https://github.com/jmamma/MCL), whose documentation of the Monomachine sysex formats guided the Library's codec
- [elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool), for its work on the Elektron OS file
  format

Bug reports and pull requests are welcome.
