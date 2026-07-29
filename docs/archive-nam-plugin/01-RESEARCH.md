# 01 — Research Findings

Condensed from web research conducted 2026-07-29. Facts below drive the design decisions in the
other docs. Items marked **VERIFY** must be re-checked against source at build time.

## 1. NeuralAmpModelerCore (the inference engine)

Repo: https://github.com/sdatkinson/NeuralAmpModelerCore — MIT license, C++20, actively developed.

**Release timeline that matters to us:**

| Version | Date | Relevance |
|---|---|---|
| v0.3.0 | 2026-01 | Real-time allocations eliminated; factory-registration extensibility |
| v0.4.0 | 2026-02 | Grouped convs, multi-in/out, FiLM, configurable activations (A2 groundwork) |
| v0.5.0 | 2026-04-16 | **A2 model support** (slimmable interfaces, container architecture, variable kernel sizes, model head) |
| v0.5.1 | 2026-04-20 | **A2 fast-path WaveNet** (hand-optimized, for A2 nano + A2 standard), `NAM_ENABLE_A2_FAST` CMake option, ON by default |
| v0.5.4 | 2026-06-24 | Eigen bumped to 5.0.1; option to disable prewarm during `Reset()`; slimmable breakpoint introspection |

**Pin to v0.5.4 or later.**

**Dependencies:** Eigen 5.0.1 (git submodule, MPL-2.0) and nlohmann/json (submodule, MIT — appears
in the public API). Nothing else at runtime.

**Sample type:** `NAM_SAMPLE` is `double` by default; compile with `-DNAM_SAMPLE_FLOAT` for
`float`. `NAM_DEFAULT_MAX_BUFFER_SIZE` is 4096.

**Loading API** (`NAM/get_dsp.h`) — all overloads return `std::unique_ptr<DSP>`:
- `get_dsp(std::filesystem::path, DspLoadOptions = {})`
- `get_dsp(const nlohmann::json& config, DspLoadOptions = {})`  ← key for us: we can load from embedded JSON, not just files
- Variants taking/returning `dspData` for caching parsed config/weights.
- File-version gate: `EARLIEST_SUPPORTED_NAM_FILE_VERSION` = "0.5.0", `LATEST_FULLY_SUPPORTED_NAM_FILE_VERSION` = "0.7.0"; validation returns `Supported::YES/PARTIAL/NO`.
- Custom architectures can be registered via `ConfigParserRegistry` / `factory::Helper` — not needed for v1.

**DSP class API** (`NAM/dsp.h`):
- `process(NAM_SAMPLE** input, NAM_SAMPLE** output, int num_frames)` — `[channel][frame]` indexing, **arbitrary frame counts** up to the `maxBufferSize` given to `Reset()`. Typical captures are mono 1-in/1-out (`NumInputChannels()`/`NumOutputChannels()`).
- `Reset(double sampleRate, int maxBufferSize)` — preallocates; **calls `prewarm()` by default** (disable via `SetPrewarmOnReset(false)`); also `ResetAndPrewarm()`.
- `prewarm()` — processes ~receptive-field samples of zeros; "somewhat expensive, should not be called during real-time audio processing"; output after prewarm is *not* silence (networks don't treat zero specially).
- `GetExpectedSampleRate()` — −1.0 if unknown; in practice 48000.
- Calibration: `Get/Set/HasInputLevel()`, `Get/Set/HasOutputLevel()` (dBu RMS at 0 dBFS peak, 1 kHz sine).
- Loudness: `Get/Set/HasLoudness()` — **`GetLoudness()` throws `std::runtime_error` if absent; always check `HasLoudness()` first.**
- Slimmable (A2): `SlimmableModel::SetSlimmableSize(double 0..1)` — thread-safe but **not real-time-safe**; `GetSlimmableSizeBreakpoints()` for UI detents. `ContainerModel` switches submodels via atomic index + mutex.

**Real-time safety:** `process()` is allocation-free after `Reset()` provided `num_frames ≤ maxBufferSize`. NOT RT-safe: `get_dsp`, `Reset()`, `prewarm()`, `SetSlimmableSize()`. **The Core does no resampling** — that's on us (§4).

**Eigen caveat:** README warns about alignment issues with some compilers/optimization settings;
fallback is `EIGEN_MAX_ALIGN_BYTES 0` + `EIGEN_DONT_VECTORIZE` at a performance cost. Only apply if
crashes appear; don't preemptively cripple SIMD.

**Bundled tools:** `run_tests`, `loadmodel`, `benchmodel` — use `benchmodel` to get real per-machine CPU numbers early.

## 2. NAM A2 (Architecture 2)

- Built by **TONE3000 in partnership with Steve Atkinson**; launched **2026-06-02**. Now the default for new TONE3000 captures; their back catalog was retrained/converted to A2.
- Architecture: stack of WaveNet-like modules — causal dilated convolutions with residual/skip connections, **LeakyReLU** (not gated tanh), a **convolutional head**, **mixed kernel sizes within a layer array**. Receptive field ~**6,350 samples (~132 ms @ 48 kHz)** vs ~4,100 for A1-Standard. Feedforward single-pass; trained with MSE + multi-resolution STFT loss on data normalized to −18 dB RMS (rescale folded into the head).
- **Slimmable models**: one `.nam` file carries two weight sets — **A2-Full (8 channels)** for desktop and **A2-Lite (3 channels)** for embedded. Runtime picks size; in Core this is `SlimmableModel`/`ContainerModel`. **VERIFY**: TONE3000 marketing says "Full/Lite" while Core release notes say the fast path covers "A2 nano + A2 standard" — reconcile naming against actual `.nam` files before surfacing labels in UI.
- Performance claims (TONE3000): A2-Full beats A1-Standard quality at **30–40% less CPU**; ~64 A2-Full or ~200 A2-Lite simultaneous instances on an M-series MacBook; A2-Lite runs at 50% CPU on a 600 MHz Cortex-M7. MUSHRA blind tests (100k+ ratings) ranked A2 above Neural DSP V2, ToneX V2, Line 6 Proxy.
- All A2 code/training/inference is **MIT**.
- **VERIFY**: exact `"architecture"` strings A2/container models use in `.nam` files (check `model_config.h` and parser registrations in Core).

## 3. The `.nam` file format

Plain JSON. Top-level keys: `version` (semver; current trainer writes "0.7.0"), `architecture`
(string: `"WaveNet"`, `"LSTM"`, `"ConvNet"`, `"Linear"`, plus A2/container strings — **VERIFY**),
`config` (arch hyperparams), `weights` (flat float array), `sample_rate` (optional; absent → −1
sentinel; in practice 48000), `metadata` (optional).

Metadata the Core reads: `loudness` (dB), `input_level_dbu`, `output_level_dbu`.
Trainer user metadata: `name`, `modeled_by`, `gear_type` (AMP, PEDAL, PEDAL_AMP, AMP_CAB,
AMP_PEDAL_CAB, PREAMP, STUDIO), `gear_make`, `gear_model`, `tone_type` (CLEAN, OVERDRIVE, CRUNCH,
HI_GAIN, FUZZ), `input_level_dbu`, `output_level_dbu`, `date`.

**Critical fact: a NAM model exposes *no* user-facing DSP knobs of its own** (parametric modeling
was removed years ago). The complete set of model-derived parameters is:
1. Expected sample rate (drives resampling decision)
2. Loudness (drives "Normalized" output mode)
3. Input/output dBu calibration (drives "Calibrated" output mode + input calibration)
4. Slimmable size selector (A2 slimmable/container models only)

Everything else (gate, EQ, trims) is plugin-side. This defines the scope of "expose any parameters
NAM exposes": output mode {Raw, Normalized, Calibrated}, input calibration level (dBu), and slim
size — per slot. File sizes: typically 50–300 KB → embedding in plugin state is trivial.

## 4. The official plugin (reference implementation)

Repo: https://github.com/sdatkinson/NeuralAmpModelerPlugin — iPlug2, MIT. v0.7.15 (2026-06) uses
Core 0.5.3; v0.7.14 added slimmable support and dropped VST2.

**Parameters it exposes** (from `EParams`, for parity checking): Input level (−20…+20 dB), Noise
gate threshold (−100…0 dB, default −80, off at min), Bass/Mid/Treble (0–10, default 5), Output
level (−40…+40 dB), gate on/off, EQ on/off, IR on/off, Calibrate-input on/off, Input calibration
level (dBu, −60…+60, default 12.0), OutputMode {Raw, Normalized, Calibrated} (default Normalized),
Slim (0.0–1.0 step 0.01).

**Output-mode math** (adopt verbatim):
- Normalized: add `(−18.0 − model.GetLoudness())` dB when `HasLoudness()`
- Calibrated: add `(model.GetOutputLevel() − inputCalibrationLevel)` dB when `HasOutputLevel()`
- Input calibration (when enabled): aligns interface dBu to the model's `input_level_dbu`

**Other DSP constants:** DC blocker at 5 Hz post-model. Noise gate (from AudioDSPTools
`dsp::noise_gate`): time 0.01, ratio 0.1, open 5 ms, hold 10 ms, close 50 ms.

**Resampling:** model wrapped in `ResamplingNAM` built on AudioDSPTools'
`ResamplingContainer<T, NCHANS, A>` (https://github.com/sdatkinson/AudioDSPTools) — dual
**Lanczos** windowed-sinc resamplers, half-width A=12 default, host→model and model→host. Latency
= chained `GetNumSamplesRequiredFor()` through both stages; **0 when host SR == model SR** (bypassed),
~a couple dozen samples at 44.1k. Reported to host; recompute + notify on model change.

**Model-swap pattern:** `_StageModel()` builds the new model off the audio thread into a staging
pointer; `_ApplyDSPStaging()` adopts it at the top of the audio callback. Copy this pattern (see
02-ARCHITECTURE for our lock-free variant).

**Tone stack** (`BasicNamToneStack` in the plugin's `ToneStack.cpp`, adopt exact constants):
three cascaded biquads, knob range 0–10, 5 = flat, gainDB = k·(value−5):
- Bass: 150 Hz, Q 0.707, 4 dB/unit (±20 dB)
- Mid: 425 Hz, Q 1.5 when cutting / 0.7 when boosting, 3 dB/unit (±15 dB)
- Treble: 1800 Hz, Q 0.707, 2 dB/unit (±10 dB)
Order: bass → mid → treble, post-model, bypassable.

## 5. Framework: JUCE vs iPlug2 (decision: JUCE)

- **JUCE 9** shipped 2026-07-21; same pricing/EULA as JUCE 8. Free **Starter** tier up to $20k annual revenue, **no splash screen since JUCE 8**; Indie $800 perpetual (to $300k); AGPLv3 dual-license option. First-class CMake since JUCE 6; `juce_add_plugin` emits VST3+AU+Standalone from one target. `juce::dsp` provides IIR biquads, `Panner` (constant-power), smoothing, `ScopedNoDenormals`.
- **iPlug2**: MIT-ish, what the official plugin uses; CMake support newer/less proven; much smaller ecosystem.
- Precedent for NAM-in-JUCE: **NAMix** (https://github.com/rations/NAMix), a JUCE 8 NAM host — proves the combination builds cleanly.
- Optional performance alternative: **mikeoliphant/NeuralAudio** (https://github.com/mikeoliphant/NeuralAudio) — hand-optimized static kernels for common WaveNet/LSTM configs, wraps NAM + RTNeural models. Keep as a swap-in option if `benchmodel` numbers disappoint; **start with official Core** for A2 fidelity and upstream tracking.

## 6. Latency and CPU facts

- **NAM models are causal (zero lookahead) → zero algorithmic latency.** Receptive field manifests as a startup transient only — hence prewarm. (arXiv:2403.08559 confirms causal-conv streaming with zero delay.)
- Resampling is the *only* latency source; 0 at 48 kHz.
- CPU anchors: A1-Standard ≈ 1.9 µs/sample ≈ ~9% of one core per instance (nam-rs bench); real-world dual-instance report ~13% CPU; A2-Full is 30–40% cheaper than A1-Standard. **Two slots ≈ 10–25% of one core worst-case (dual A1-Standard); much less with A2.** Comfortably feasible; no oversampling of the NN needed or wanted.
- Lesson from official-plugin issues #553/#255: CPU pathologies came from ancillary DSP (gate/EQ/normalization) left running or badly bypassed, not the network. Make bypasses true bypasses.

## 7. Dual-amp prior art

- **NAM Universal** (WaveMind, free): true stereo I/O, two NAM loaders in series (pedal+amp), mono or true-stereo input modes.
- **Nam XT** (iOS/macOS AUv3): dual NAM loaders, stereo placement per slot.
- **NeuralAmpModelerPluginStereo**: community stereo fork of the official plugin.
- **Two Notes GENOME**: hosts `.nam` in CODEX blocks, parallel dual paths.
- **Tonocracy** (free): NAM-compatible, cloud capture training (~15 min).
- Nobody ships exactly "two captures, pan L/R, per-amp tone stack" as a focused product — our niche is real.

## 8. Capture creation (full detail in 04-CAPTURE.md)

- Standard workflow: reamp the official test signal **`v3_0_0.wav`** (48 kHz / 24-bit / mono, ~3 min: validation segment, blips, chirps, noise, program material) through the gear; record the return; train with the MIT Python `neural-amp-modeler` package (GUI/Colab/TONE3000 cloud).
- The trainer identifies signal version by **MD5 hash** (v3 = `ede3b9d82135ce10c7ace3bb27469422`) and auto-aligns latency via blip correlation. Quality gates: blip-consistency and validation-replicability at **ESR ≤ 0.01**; post-training validation ESR score (<0.01 great, <0.035 decent, ≥0.3 broken).
- **No C++ training path exists anywhere** in the ecosystem, deliberately. All training routes are PyTorch. ONNX export was removed in trainer v0.12.0.
- Shipped precedents: **ToneX** trains locally in its standalone app (2–30 min); **Two Notes Capture Studio** (2026, free) records + trains fully offline locally and exports A2/Standard/Lite/Feather/Nano `.nam` — the strongest precedent for our design; **Tonocracy** trains in the cloud; **TONE3000** offers free cloud training on RTX 4090s (~10 min for A2 on a free Colab GPU as another anchor). **SmartAmpPro** (GuitarML) is the cautionary tale: in-plugin capture that shelled out to *user-installed* Python — the dependency killed it.
- The trainer officially supports **Apple Silicon MPS** (dedicated env file; MPS bug workarounds maintained; A2's MRSTFT loss fixed for Apple Silicon in v0.12.1). No published M-series benchmark; planning estimate 15–45 min per capture. **VERIFY empirically early** (30-min experiment: `pip install neural-amp-modeler`, train sample files MPS vs CPU).
- Trainer package versions: `neural-amp-modeler` **v0.13.0** added A2 training (June 2026).

## 9. Licensing chain

| Component | License | Notes |
|---|---|---|
| NeuralAmpModelerCore | MIT | |
| A2 architecture/inference/training | MIT | TONE3000: "free to use, modify, ship commercially" |
| Eigen 5.x | MPL-2.0 (fully — LGPL parts relicensed in 5.0.0) | file-level copyleft; fine in closed binaries; publish only if we modify Eigen files |
| nlohmann/json | MIT | |
| AudioDSPTools | MIT | if we vendor `ResamplingContainer`/tone stack/gate |
| neural-amp-modeler (Python) | MIT | PyTorch BSD-3, Lightning Apache-2.0, librosa ISC |
| JUCE | Starter free (<$20k rev) / AGPLv3 / Indie $800 | choose at build time; Starter is fine to start |

**No GPL anywhere in the required chain.** (Do not copy code from the community LV2 plugin without
checking its license separately.)

## Verified during M0 (2026-07-29, Apple Silicon, macOS 26.2)

Resolved against the actual v0.5.4 source and example models, superseding the
assumptions above where they differ:

1. **Architecture strings** in real `.nam` files are `WaveNet`, `LSTM`, `ConvNet`, `Linear`, and `SlimmableContainer`. **A2 has no distinct architecture string** — A2 models are either `WaveNet` (e.g. `wavenet_a2_max.nam`, file version 0.6.0) or `SlimmableContainer` (e.g. `A2.nam`, version 0.7.0). Consequence: never detect A2 or slimmability by name. Detect slimmable support with `dynamic_cast<nam::SlimmableModel*>`, which is what `ResamplingNam::getSlimmableModel()` does.
2. **Registration is the real trap.** Architectures self-register via file-scope statics (`static nam::ConfigParserHelper _register_WaveNet(...)` in `NAM/wavenet/model.cpp`, likewise LSTM). A normal static-library link discards those objects and *every* model fails with "No config parser registered for architecture: WaveNet". Fixed by linking nam_core with `$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>`, propagated PUBLIC so the VST3/AU/Standalone links keep it.
3. **Resampler latency measured: 27 samples** for a 48 kHz model at a 44.1 kHz host (Lanczos A=12, both directions). **0 samples at 48 kHz**, as designed.
4. **`ResamplingContainer` is not standalone** — it is an iPlug2 port that still references `iplug::PI` and `DEFAULT_BLOCK_SIZE`. `src/dsp/ResamplerShim.h` supplies both with iPlug2's values and includes the container.
5. **nlohmann is included as `<json.hpp>`**, not `<nlohmann/json.hpp>`; NAM Core puts `Dependencies/nlohmann` directly on the include path.
6. **Measured CPU** (this machine, 128-sample blocks at 48 kHz, model + tone stack, % of one core):

   | Model | realtime × | 1 slot | 2 slots |
   |---|---|---|---|
   | `wavenet_a1_standard.nam` | 11.7 | 8.51% | 17.0% |
   | `A2.nam` (slimmable container) | 28.2 | 3.54% | 7.1% |
   | `wavenet_a2_max.nam` | 30.0 | 3.34% | 6.7% |
   | `lstm.nam` | 213 | 0.47% | 0.9% |

   A1 Standard matches the published ~9%/instance figure. A2 is ~2.5× cheaper than A1 Standard — better than the advertised 30–40% saving. Dual amps are comfortably affordable; no need for the NeuralAudio alternative engine.
7. **Block-size independence is exact**: processing in 64- vs 512-sample blocks produces bit-identical output (max delta 0.0) for every architecture tested. Verified in `tests/dsp_tests.cpp`.

## Open items still to verify

1. "A2 nano/standard" vs "A2 Lite/Full" naming, for slim-slider detent labels.
2. MPS training time on this Mac (empirical) — needed before committing to the local-training UX in M6.
3. Whether TONE3000 exposes a public upload API (do not assume; otherwise link out to their site).
4. Current `neural-amp-modeler` pip version and its Python-version requirement when building the helper.
