# Audio references (44.1 kHz)

Recordings of the plugin processor at 44.1 kHz, the engine's native rate, where its output passes to the host
untouched. They are the "before" side for the resampling rework (issue #3): after the rework, the same scenarios are
rendered at other sample rates and compared against these, and the 44.1 kHz renders must stay bit-identical.

- `manifest.json`: provenance (git commit, OS file hash, date) and, per scenario, the file, a SHA-256 of its samples,
  levels, and the results of the capture's own checks: each scenario was rendered twice at block size 512 (identical =
  deterministic) and once each at 32, 441, 2048 and random sizes 1-1024 (identical = independent of the host's block size).
- `<scenario>.44100.wav`: 32-bit float, exactly the samples `processBlock` produced (Monomodule Six: 12 channels, its six
  stereo outputs). `<scenario>.44100.input.wav`: the input signal of the FX and side-chain scenarios.

The WAV files are not in git (`*.wav` is ignored). The manifest's hashes still identify them, so a fresh capture can be
checked against this one without the files.

## Regenerating

Built with the plugins (`MNM_BUILD_PLUGIN`), the harness is the `MnmCapture` target (`src/plugin/Capture.cpp`):

```bash
MnmCapture --list
MnmCapture --out tests/audio-reference --fw /path/to/Elektron_SFX6-60_OS1.32B.syx --inputs --note "why this capture"
```

Scenarios are fixed in code: notes, CC automation and input signals sit on a timing grid that is a whole number of
samples at 44.1/88.2/176.4 and 48/96/192 kHz, so each event can reach the emulated DSP in the same 16-frame block at
every rate. Input signals are defined in continuous time, so every rate samples the same signal.

## Comparing a capture with these

```bash
MnmCapture --out build/captures/48000 --fw /path/to/os.syx --rates 48000
python3 tests/compare_captures.py --ref tests/audio-reference --test build/captures/48000
```

`compare_captures.py` (numpy and scipy) requires 44.1 kHz renders to be bit-identical to these. A render at another
rate is moved back by the latency its manifest records for the sample-rate conversion, converted to 44.1 kHz offline
with a far longer filter than the plugin's, and must match these in 20 Hz-18 kHz: overall difference at least 80 dB
down, no 10 ms stretch less than 60 dB down, third-octave levels within 0.05 dB, pitch within 0.1 cent, no residual
delay, and identical output whatever block sizes the host uses. Its docstring explains each measure.
