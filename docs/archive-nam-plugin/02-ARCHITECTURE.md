# 02 — Technical Architecture

## Targets and toolchain

- **Framework:** JUCE 8/9 via CMake (`juce_add_plugin`). Formats: **VST3, AU, Standalone** (the Standalone target doubles as the capture app host). macOS first (arm64 + x86_64 universal), Windows later — keep everything CMake-portable, no Xcode-project dependence.
- **Engine:** NeuralAmpModelerCore ≥ v0.5.4 as a git submodule, built as a static lib with `NAM_ENABLE_A2_FAST=ON`. Also vendor **AudioDSPTools** (MIT) for `ResamplingContainer`, `noise_gate`, and as the reference for the tone stack.
- **Sample type decision:** build Core with default `NAM_SAMPLE = double`. JUCE processes float; convert at the slot boundary (interleave/deinterleave is already needed for `[channel][frame]` pointers, so the copy is free). If profiling shows the double path costs too much, switch to `-DNAM_SAMPLE_FLOAT` — keep this a single CMake toggle.
- **C++20** throughout (Core requires it).
- Repo layout:

```
NAM-Modeler/
├── CMakeLists.txt
├── third_party/            # submodules: JUCE, NeuralAmpModelerCore, AudioDSPTools
├── src/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── dsp/
│   │   ├── AmpSlot.{h,cpp}         # one amp channel: trim→NAM→EQ→trim→pan
│   │   ├── ResamplingNam.{h,cpp}   # NAM DSP wrapped in ResamplingContainer
│   │   ├── ToneStack.{h,cpp}       # 3-band biquad stack (NAM constants)
│   │   ├── ModelLoader.{h,cpp}     # background load/prewarm/stage/swap
│   │   └── Meters.{h,cpp}
│   ├── state/
│   │   ├── Parameters.{h,cpp}      # APVTS layout (see 03-PARAMETERS.md)
│   │   └── PluginState.{h,cpp}     # embedded-model serialization
│   └── capture/                    # see 04-CAPTURE.md
├── resources/                      # v3_0_0.wav test signal, UI assets
└── docs/
```

## Signal flow

```
                       ┌─────────────────────────────────────────────────┐
                       │ AMP SLOT A                                      │
              ┌──────► │ InTrim → [Gate] → NAM(A) → DCBlock → ToneStack │──► OutTrim → Pan A ─┐
 Input ─ mode │        │                    ▲ (Raw/Norm/Cal gain)        │                     ├─► Σ → Master Out → [Stereo/Mono] → Output
 (Mono/Stereo)│        └─────────────────────────────────────────────────┘                     │        Trim
              │        ┌─────────────────────────────────────────────────┐                     │
              └──────► │ AMP SLOT B   (identical chain)                  │──► OutTrim → Pan B ─┘
                       └─────────────────────────────────────────────────┘
```

- **Input modes:**
  - `Mono` (default): guitar DI feeds both slots. If the host bus is stereo, use the left channel (matches guitar-interface convention); if mono, that channel.
  - `Stereo`: L → slot A, R → slot B (enables upstream stereo effects and NAM-Universal-style routing).
- **Per-slot chain order:** input trim → optional noise gate → NAM model (mono in/out) → DC blocker (5 Hz highpass) → output-mode gain (Raw/Normalized/Calibrated, from model metadata) → tone stack → output trim → phase invert → pan.
- **Pan:** constant-power sin/cos law (−3 dB center) — `juce::dsp::Panner` with `Rule::sin3dB`. Defaults: A hard left is *not* the default; both center. (User pans as desired; presets can ship A=−1.0, B=+1.0.)
- **Sum:** slot A + slot B stereo outputs summed; master output trim; optional mono-sum switch for compatibility checks. Per-slot solo/mute before the sum.
- **Bypassed slot** (no model loaded or slot disabled): output silence from that slot, not dry pass-through — a dual-amp plugin passing dry signal on one side is a footgun. Global plugin bypass is the host's standard bypass (dry).
- All gain parameters smoothed (`juce::SmoothedValue`, ~20 ms); pan smoothed likewise. Tone-stack coefficient updates per-block on parameter change (Qs are low; zipper risk negligible — if audible, smooth the gain input to the coefficient calc).

## Real-time safety rules (non-negotiable)

1. **Audio thread never**: allocates, locks a contended mutex, touches the filesystem, parses JSON, calls `get_dsp`/`Reset`/`prewarm`/`SetSlimmableSize`, logs, or destroys a model.
2. `nam::DSP::process()` is allocation-free only after `Reset(sr, maxBlock)` with sufficient maxBlock — call `Reset` in `prepareToPlay` with the host's max block size, and **chunk defensively** if a host ever delivers more frames than promised.
3. `ScopedNoDenormals` in `processBlock`.
4. Meter values out via atomics; no message-thread reads of DSP objects.

## Model loading & swapping (the core threading pattern)

Adapted from the official plugin's staging pattern, made lock-free for JUCE:

1. **UI/message thread** (or a dedicated loader thread via `juce::ThreadPool`): read file / take embedded JSON → `nam::get_dsp(json)` → wrap in `ResamplingNam` → `Reset(hostSR, maxBlock)` → `prewarm()` (expensive — this is why it's off-thread) → publish into a per-slot staging area:
   `std::atomic<Slot::StagedModel*> staged` (pointer swap, release/acquire).
2. **Audio thread**, top of `processBlock`: `staged.exchange(nullptr)`; if non-null, move the old active model into a **retirement queue** (lock-free SPSC to the loader thread) and adopt the new one. **Never delete on the audio thread** — the loader thread drains the retirement queue.
3. **Crossfade** ~20–50 ms between old and new model output on swap to mask the splice (run both for the fade duration — CPU spike is bounded and brief). v1 fallback if this complicates the build: short fade-out/fade-in with the new model only.
4. **Latency notification:** after adopting a model, if resampler latency changed, flag it; message thread calls `setLatencySamples()` (JUCE handles host notification). Latency = max across slots (equal in practice — same policy both slots).
5. **Slim-size changes** (A2): `SetSlimmableSize` is thread-safe but not RT-safe → treat like a mini model-swap: apply from the loader thread; Core's container handles the internal switch. Debounce slider drags (apply on ~100 ms settle).
6. **Sample-rate / block-size change** (`prepareToPlay`): rebuild/`Reset` both models synchronously (audio isn't running during `prepareToPlay`), re-prewarm, recompute latency.

## Resampling & latency policy

- Each slot's NAM model is wrapped in a `ResamplingNam` modeled on the official plugin's `ResamplingNAM` + AudioDSPTools `ResamplingContainer<NAM_SAMPLE, 1, 12>` (Lanczos, A=12, one channel per slot).
- `hostSR == model.GetExpectedSampleRate()` (or model SR unknown, −1) → container bypassed, **zero added latency**. This is the headline low-latency path: at 48 kHz the plugin reports 0 samples.
- Otherwise: dual Lanczos resample, latency from the container's chained `GetNumSamplesRequiredFor()`, reported to host. Expect ~tens of samples at 44.1 kHz.
- Never resample the whole plugin — trims/EQ/pan run at host rate; only the NAM inference is rate-wrapped. If the two slots' models have different expected rates (rare), each wraps independently and reported latency is the max; **align the shorter path with a delay line** so A/B stay phase-coherent (v1 may simply document the mismatch and pad; implement the delay in M4).
- Prewarm covers both the network receptive field (`GetPrewarmSamples()`, ~6.4k samples for A2) and the resampler priming (ResamplingContainer pre-warms itself with silence).

## CPU budget

Worst case dual A1-Standard ≈ 10–25% of one core at 48 kHz; A2-Full ≈ 30–40% cheaper. No
oversampling of the network. Ancillary DSP (gate/EQ) must be truly skipped when bypassed (see
official-plugin issues #553/#255 — their idle-CPU bugs came from ancillary DSP, not the NN). Run
Core's `benchmodel` on target hardware in M1 to set real numbers.

## State & presets

**Embed the model; keep the path as metadata.** Serialization (JUCE `ValueTree` under APVTS state,
binary-safe):

```
state/
├── params            # all APVTS parameters (03-PARAMETERS.md)
├── slotA/
│   ├── modelJson     # full .nam file content, gzip-compressed (juce::GZIPCompressor…)
│   ├── modelPath     # original absolute path — display + re-link only, never required
│   └── modelName     # display name (from metadata.name or filename)
└── slotB/ …
```

- Restore path: decompress → `nlohmann::json::parse` → `get_dsp(json)` → normal staging pipeline. Sessions survive moved/deleted `.nam` files and machine transfers (fixes the official plugin's known weakness of path-only state).
- `.nam` files are 50–300 KB; gzipped weights compress well; state size is a non-issue.
- Factory/user presets: same format, stored as `.nammod` preset files (ValueTree binary or XML+base64) in the user preset folder; include the embedded models so presets are portable.
- Parameter IDs are versioned (`v1` suffix convention or explicit state version int) — plan for additive change.

## UI (keep simple; DSP correctness is the product)

- One window, two identical amp-slot strips (A left, B right): model name + load button + drag-drop target, input trim, Bass/Mid/Treble, output trim, pan, phase, solo/mute, output-mode selector, calibration dBu field (visible when mode = Calibrated or calibrate-input on), slim slider (visible only when the loaded model is slimmable, with detents from `GetSlimmableSizeBreakpoints()`).
- Center/bottom strip: input mode, master out trim, mono-sum, gate threshold + enable, meters (in, per-slot out, master out), Capture button (opens capture wizard — 04-CAPTURE.md).
- Drag-drop `.nam` onto a slot; double-click model name to open file chooser; show model metadata (gear make/model, tone type, sample rate, calibration presence) in a tooltip/inspector.
- Use plain JUCE components + `GenericAudioProcessorEditor` as the M2 stopgap so DSP milestones never block on UI.

## Error handling

- Unsupported/corrupt `.nam` (version gate returns `NO`, parse throws): surface a non-modal slot error badge with the reason; keep the previous model running.
- `Supported::PARTIAL`: load but show a caution badge.
- Model with unknown sample rate (−1): assume 48 kHz, badge it.
- Missing loudness/calibration metadata: grey out Normalized/Calibrated options respectively (fall back to Raw gain of 0 dB) — mirror official-plugin behavior.
