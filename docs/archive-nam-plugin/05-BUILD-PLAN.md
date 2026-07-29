# 05 — Build Plan

Phased milestones for implementation. Each milestone ends in a **verifiable state** — build it,
run the check, then move on. DSP correctness milestones (M1–M4) never block on UI; use
`GenericAudioProcessorEditor` until M6.

## Status as of 2026-07-29

**M0–M4 are complete and verified.** M7 has a working functional editor (real visual design still
to do). M5/M6 (the capture wizard and trainer helper) have not been started.

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel 8`
Test: `ctest --test-dir build --output-on-failure` (or run `build/dsp_tests`,
`build/plugin_tests_artefacts/Release/plugin_tests`, `build/bench` directly, each taking the
example-models directory as its argument).

Verified so far:
- VST3, AU and Standalone all build; **`auval -v aufx Nmd1 Nmdl` passes** (renders 11–192 kHz, blocks 64–4096).
- `tests/dsp_tests.cpp`: every example model (A1 WaveNet, LSTM, A2, slimmable container) loads and renders; **block size 64 vs 512 is bit-identical**; latency is 0 at 48 kHz and 27 samples at 44.1 kHz; tone stack curves match the spec (+17.5 dB at 80 Hz with bass at 10, +9.8 dB at 5 kHz with treble at 10).
- `tests/plugin_tests.cpp`: silence with no model (not dry pass-through); hard-panned amps land in the correct channels; solo/mute/enable behave; **Normalized output mode measured +2.02 dB against an expected +2.02 dB**; state round-trips from the embedded model with a 0.00 dB level difference; rapid model swapping under continuous audio stays finite and stable.
- `tests/bench.cpp`: A1 Standard 8.5% of a core per slot, A2 3.4%, LSTM 0.5%.

Not yet done, in rough priority order: crossfade on model swap; per-slot metering in the UI; the
real visual design; the capture wizard (M5) and trainer helper (M6); Windows build.

## M0 — Skeleton & de-risking (½–1 day)

- CMake superbuild: JUCE (submodule or FetchContent) + NeuralAmpModelerCore ≥ v0.5.4 (submodule, `NAM_ENABLE_A2_FAST=ON`) + AudioDSPTools (submodule). `juce_add_plugin` target: VST3 + AU + Standalone, C++20, universal binary on macOS.
- Empty pass-through processor builds and loads in a DAW and in `pluginval` (add pluginval to CI habits from day one; strictness level 8+).
- Build Core's `benchmodel` and record per-instance CPU numbers on the dev machine for an A1-Standard and an A2 model. **Resolve the VERIFY items from 01-RESEARCH §"Open items"** (A2 architecture strings, slim naming) by reading Core source in `third_party/`.
- De-risk training early (parallel task, no code dependency): `pip install neural-amp-modeler` in a scratch venv, train the docs' sample files on MPS, record wall-clock. This number shapes M7's UX copy (progress bar vs "go make coffee").
- **Done when:** plugin loads in a DAW; pluginval clean; benchmodel + training timings written into `docs/01-RESEARCH.md`.

## M1 — One slot, mono, hardcoded model (1–2 days)

- `AmpSlot` with NAM model only (no trims/EQ yet). Load a `.nam` path hardcoded or via a basic file-chooser button. Background loader thread + staging/retirement swap pattern from 02-ARCHITECTURE (build the real pattern now — do not prototype with a mutex and "fix later").
- `Reset` + prewarm off-thread; `ScopedNoDenormals`; double↔float conversion at slot boundary.
- **Done when:** guitar through one capture sounds correct at 48 kHz, 64–128-sample buffers; no allocation in `processBlock` (verify with a debug allocator assert or Instruments); model can be swapped repeatedly while audio runs with no glitch/crash; A1 and A2 files both load.

## M2 — Dual slots, stereo, pan, trims, EQ (2–3 days)

- Second slot; input-mode routing (Mono/Stereo); per-slot input trim, output trim, phase, pan (constant-power), solo/mute; master out, mono-sum.
- Tone stack: 3 biquads with the exact official constants (01-RESEARCH §4), true bypass.
- DC blocker (5 Hz) post-model. Optional gate (official constants) — can slip to M5 if needed.
- Full APVTS layout per 03-PARAMETERS.md (all IDs final now — they're frozen forever after first release).
- **Done when:** two different captures pan hard L/R and null against expectations; EQ curves measured (white-noise + spectrum or pluginval's audio checks) match the stated Hz/Q/dB constants; automation of every parameter is click-free.

## M3 — NAM-exposed parameters & state (1–2 days)

- Output mode Raw/Normalized/Calibrated with the official gain math; calibrate-input; interface-dBu parameter; metadata-driven UI availability (grey out modes the model lacks).
- Slim-size parameter wired to `SetSlimmableSize` via the loader thread (debounced), detents from breakpoints, hidden for non-slimmable models.
- State: embed gzipped model JSON per slot + path/name metadata; restore via `get_dsp(json)`. Preset save/load as portable files.
- **Done when:** save a session in a DAW, delete/move the `.nam` files, reopen — identical sound. Slim slider audibly/CPU-measurably switches sizes without audio dropout. Output modes match the official plugin's levels A/B'd on the same capture.

## M4 — Resampling & latency (1–2 days)

- `ResamplingNam` wrapper (ResamplingContainer, Lanczos A=12): bypass at matching SR, resample otherwise; latency computation, `setLatencySamples`, change notification on model load; inter-slot delay alignment for mismatched model rates.
- **Done when:** at 44.1/88.2/96 kHz host rates a 48 kHz capture sounds identical (null test vs 48 kHz render within resampler tolerance); reported latency verified with a host loopback ping; 48 kHz reports 0.

## M5 — Capture wizard: record & validate (3–5 days)

- Standalone-only wizard per 04-CAPTURE.md steps 1–6: device pick, level check, sample-locked play/record of bundled `v3_0_0.wav`, C++ validation suite (blip latency detection, blip-consistency ESR, validation-replicability ESR, thresholds per 04-CAPTURE), alignment plot, metadata form, `.namcapture` bundle writer, TONE3000 export.
- Unit-test the validators against known-good and deliberately-broken recordings (add a delay → latency detection finds it; add chorus → replicability check fails).
- **Done when:** a real reamp session (or loopback simulation: play test signal through a NAM model in-process as the "amp") produces a bundle that the *official* Python trainer accepts and trains without complaints — that's the compatibility oracle.

## M6 — Trainer helper & training UI (3–5 days)

- Bundled Python env + pinned `neural-amp-modeler`; thin job-runner wrapper; progress-streaming protocol; wizard step 7–8 UI (progress, cancel, ESR result bands, one-click load into slot). Codesign/notarize the embedded interpreter.
- **Done when:** end-to-end: record (loopback oracle) → validate → train A2 on MPS → resulting `.nam` loads in slot A and sounds like the source model; cancel mid-train leaves no orphan processes.

## M7 — UI & polish (3–5 days)

- Real editor per 02-ARCHITECTURE UI sketch: two amp strips, center utility strip, meters, drag-drop, model inspector, error badges.
- Crossfade on model swap (if not already in M1), smoothing audit, mono-compat pass, CPU idle audit (bypassed slot ≈ 0 cost; gate/EQ off = truly off).
- pluginval strictness 10 clean; test matrix: Logic (AU), Reaper (VST3), Ableton, at 44.1/48/96 kHz, 32–2048 buffers.

## Explicit non-goals for v1 (keep scope honest)

- IR loader (official plugin has one; our per-amp EQ + captures-with-cab cover v1 — revisit v2)
- Windows build (keep CMake portable; ship after macOS is solid)
- CLAP format, AAX
- Serial amp chaining (pedal→amp) — the two slots are parallel by design; revisit v2
- Cloud training infra of our own

## Risk register

| Risk | Mitigation |
|---|---|
| A2 naming/arch-string assumptions wrong | Resolved in M0 by reading Core source; nothing user-facing depends on names until M3 |
| MPS training too slow for good UX | Measured in M0; fallback = lead with TONE3000 export, local training as "advanced" |
| Eigen alignment crash on some toolchain | Known workaround (01-RESEARCH §1); apply only if it appears |
| Embedded-Python notarization pain | Known-solved (many shipped apps); budget it in M6; worst case: helper as separate signed .pkg |
| Dual-model CPU on old Intel Macs | benchmodel numbers in M0; A2-Lite/slim + per-slot enable give users an out |
| Host delivers > maxBufferSize frames | Defensive chunking in the slot process loop (cheap, M1) |
