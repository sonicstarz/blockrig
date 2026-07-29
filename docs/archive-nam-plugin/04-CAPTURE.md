# 04 — Capture Creation Design

Goal: create new `.nam` captures from real amps/pedals, end-to-end, without the user installing
Python or leaving the app. Design follows the industry-converged pattern (Two Notes Capture
Studio, ToneX): **record + validate in native code; train via a bundled copy of the official
trainer**. We never reimplement training in C++ — the recipe churns upstream (A1→A2 retrained the
whole ecosystem in mid-2026) and a fork would be permanently stranded.

## Architecture: three pieces

1. **Capture wizard** (C++/JUCE, lives in the plugin + standalone): plays the test signal, records the return, runs all pre-flight validation, writes a *capture session bundle*.
2. **Trainer helper**: a private, bundled CPython environment running the unmodified MIT `neural-amp-modeler` package (v0.13.0+, A2-capable). Invoked as a child process by the standalone app with a JSON job file; reports progress over stdout lines the app parses. macOS: MPS-accelerated; ~0.5–1 GB added to the installer (CPU/MPS wheels only — no CUDA on macOS). Windows later: CPU wheels by default, optional CUDA download.
3. **Cloud alternative (documented, not built):** "Export session for TONE3000" writes the input/output WAV pair the user can upload to TONE3000's free trainer (RTX 4090s, A2 output). Do **not** assume a programmatic API — verify at build time; otherwise this is a "reveal files + open browser" flow.

The **Standalone target owns capture** (direct device access, no host routing weirdness). Inside a
DAW, the Capture button explains the routing requirement and offers to launch the standalone. This
matches ToneX/Capture Studio and avoids fighting host I/O.

## Capture session flow (wizard steps)

1. **Setup**: pick output channel (→ reamp box → gear) and input channel (gear/mic return). Ship `v3_0_0.wav` (48 kHz/24-bit/mono, MD5 `ede3b9d82135ce10c7ace3bb27469422`) as a bundled resource. If device can't do 48 kHz, run the device at its rate but resample playback and record path to 48 kHz for the session files (or simply require 48 kHz in v1 — simplest correct thing).
2. **Level check**: play a short loop (the noise/chirp section), show input meter; warn on clipping (> −0.1 dBFS) or too-cold signal (< −40 dBFS peak).
3. **Optional calibration** (differentiator — almost nobody fills these fields): guide the user through the official dBu procedure — measure send level with a multimeter on a 1 kHz 0 dBFS sine (dBu = 20·log10(V_RMS / 0.7746)), enter the reading → becomes `input_level_dbu`; loopback-assisted measurement for `output_level_dbu`. Skippable; fields left absent if skipped.
4. **Record**: play `v3_0_0.wav` sample-locked while recording the return; write both files. Recorded file must be ≥ input length (trainer tolerates 0–1 s longer, never shorter) — record input-length + 1 s.
5. **Validate (C++ pre-flight — reimplement the trainer's checks so failures surface *before* a 20-minute train):**
   - Sample-rate and length checks.
   - **Latency alignment** via blip detection, mirroring `nam/train/core.py`: background-level estimate; trigger threshold `max(background + 0.0003, 1.001 × background)`; scan with 1,000-sample lookahead / 10,000-sample lookback; average across blips; subtract 1-sample safety factor. Flag if per-blip estimates disagree by ≥ 20 samples. Show the alignment plot for eyeball confirmation; allow manual `latency_samples` override.
   - **Blip consistency**: consecutive blips, and start-vs-end blips, must match within **ESR ≤ 0.01** (catches tube drift, knob bumps, level changes mid-session).
   - **Validation replicability**: the two identical validation segments in v3 must match within **ESR ≤ 0.01** (catches noise floors and time-based FX — chorus/delay/reverb in the chain make gear uncapturable; tell the user exactly that).
   - ESR = Σ(target − candidate)² / Σ(target)² over the segment; trivial C++.
   - Offer "proceed anyway" (the trainer has the same skip-checks escape hatch).
6. **Metadata**: name, gear make/model, `gear_type` (AMP, PEDAL, PEDAL_AMP, AMP_CAB, AMP_PEDAL_CAB, PREAMP, STUDIO), `tone_type` (CLEAN, OVERDRIVE, CRUNCH, HI_GAIN, FUZZ), modeled_by; calibration dBu values from step 3 if measured.
7. **Train**: architecture choice — **A2 (default; produces slimmable Full+Lite in one run)** or legacy A1 Standard/Lite/Feather/Nano for embedded-target users. Kick the helper; stream epoch/ESR progress into the wizard; training is cancelable. Planning estimate on Apple Silicon (MPS): 15–45 min — **validate empirically in M0** (see 05-BUILD-PLAN).
8. **Result**: show final validation **ESR with the official quality bands** (<0.01 "great", <0.035 "not bad", <0.1 "might sound ok", <0.3 "probably won't sound great", ≥0.3 "something went wrong"); write the `.nam`; offer "Load into Amp A/B" one-click.

## Capture session bundle format

A folder (or zip) the wizard writes before training — makes sessions re-trainable, shareable, and
cloud-exportable:

```
MyAmp_2026-07-29.namcapture/
├── session.json      # device info, latency estimate, checks results, metadata, calibration
├── input.wav         # the v3_0_0 signal actually played (copied, for self-containment)
└── output.wav        # recorded return, untrimmed
```

Training consumes the bundle; the helper's job file points at it. "Train later / batch training"
(ToneX-style) falls out of this design for free.

## Trainer helper details

- Distribution: a relocatable Python env (e.g. `python-build-standalone` + venv, or briefcase-style bundle) inside the app bundle/`Application Support`, first-run unpacked. Code-sign and notarize all binaries inside (macOS requirement — known-solved problem, but budget time for it).
- Invocation: `helper/bin/python -m nam_modeler_train job.json` — a thin ~100-line Python wrapper around `nam.train` APIs that reads the job file, runs training with the chosen config (A2 = the packed/slimmable config), emits `PROGRESS {"epoch":n,"esr":x}` lines, writes the `.nam` with our metadata merged.
- Version-pin `neural-amp-modeler` in the helper; upgrading the pin is a normal maintenance task and inherits upstream architecture changes with zero C++ churn.
- The helper is invoked only by the **standalone** process, never from inside a DAW-hosted plugin instance (process lifetime + GPU contention inside a DAW is hostile; nobody ships that).

## Explicitly rejected alternatives (and why)

- **libtorch training in C++**: 0.2–2.5 GB of libs, a permanent fork of a moving training recipe, no upstream support, no shipped precedent. Rejected.
- **Depending on user-installed Python**: killed GuitarML SmartAmpPro. Rejected.
- **In-DAW training**: process lifetime/GPU contention/host stability. Rejected — standalone only.
- **Owning cloud training infra**: competing with free TONE3000 with worse economics. Link out instead.
