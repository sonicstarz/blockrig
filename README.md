# NAM Modeler — Dual-Capture Stereo NAM Plugin

**Status: planning/design phase. No code yet — these documents are the groundwork for implementation.**

A low-latency stereo audio plugin (VST3 / AU / Standalone) that runs **two Neural Amp Modeler
captures simultaneously** — including the new **NAM A2** architecture — each pannable left/right,
with per-amp input/output trim and a 3-band amp-style tone stack. It also includes a **capture
wizard** for creating new `.nam` captures of real amps and pedals.

## Feature summary

- Two independent NAM amp slots (A and B), each loading any `.nam` file (A1 WaveNet, LSTM, ConvNet, Linear, and A2 including slimmable models)
- Per-slot signal chain: input trim → NAM model → 3-band tone stack (Bass/Mid/Treble) → output trim → pan
- Stereo routing: mono guitar into both amps (the primary use case) or true stereo (L→A, R→B); constant-power panning; per-slot phase invert, solo, and mute
- Every parameter NAM exposes, surfaced per slot: output mode (Raw / Normalized / Calibrated), input calibration level (dBu), and A2 slimmable model size
- Zero algorithmic latency at 48 kHz; transparent high-quality resampling (with host latency reporting) at other sample rates
- Plugin state embeds the full `.nam` model data, so sessions survive moved/deleted files and transfer between machines
- Capture creation: guided capture session (plays the official NAM test signal, records the return, validates alignment and quality in real time) with local training via the official MIT-licensed NAM trainer

## Documents

| Doc | Contents |
|---|---|
| [docs/01-RESEARCH.md](docs/01-RESEARCH.md) | Condensed research findings: NAM Core API, A2 architecture, `.nam` format, CPU/latency facts, prior art, licensing — with sources |
| [docs/02-ARCHITECTURE.md](docs/02-ARCHITECTURE.md) | Technical architecture: framework, signal flow, threading model, real-time safety, resampling/latency, state format |
| [docs/03-PARAMETERS.md](docs/03-PARAMETERS.md) | Complete parameter specification: IDs, ranges, defaults, skews, automation behavior |
| [docs/04-CAPTURE.md](docs/04-CAPTURE.md) | Capture-creation feature design: recording flow, validation checks, training pipeline |
| [docs/05-BUILD-PLAN.md](docs/05-BUILD-PLAN.md) | Phased implementation plan with milestones, acceptance criteria, and verification steps |
| [CLAUDE.md](CLAUDE.md) | Standing guidance for the implementing agent |

## Key decisions (rationale in the docs)

1. **Framework: JUCE 8/9 + CMake** (free Starter tier; `juce::dsp` covers filters/pan/smoothing; best-documented path for VST3+AU+Standalone from one target). NAM Core is framework-agnostic C++ and is used directly.
2. **Engine: NeuralAmpModelerCore v0.5.4+** (MIT) with `NAM_ENABLE_A2_FAST=ON` — official A2 support including the hand-optimized fast path.
3. **Latency policy**: no added latency at 48 kHz (models are causal, zero lookahead); Lanczos resampling via AudioDSPTools' `ResamplingContainer` pattern otherwise, reported to the host.
4. **Tone stack**: the official NAM plugin's proven biquad constants (Bass 150 Hz / Mid 425 Hz adaptive-Q / Treble 1.8 kHz), per slot, post-model.
5. **State**: embed `.nam` JSON per slot (50–300 KB each — trivial), keep the original path only as display/re-link metadata.
6. **Capture**: recording + validation in C++ inside the plugin/standalone; training handed to a bundled-Python helper running the unmodified official `neural-amp-modeler` trainer (MPS-accelerated on Apple Silicon), with "upload to TONE3000" as the documented cloud alternative. No C++ training fork.

## Licensing at a glance

Everything in the required chain is permissive: NeuralAmpModelerCore (MIT), A2 architecture/training/inference (MIT), nlohmann/json (MIT), Eigen 5.x (fully MPL-2.0, file-level copyleft only), neural-amp-modeler Python trainer (MIT), PyTorch (BSD-3). JUCE is free (Starter) up to $20k annual revenue with no splash screen, or AGPLv3 if the plugin is open-sourced, or $800 perpetual Indie. **No GPL anywhere.**
