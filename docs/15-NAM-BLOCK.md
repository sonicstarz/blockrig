# 15 — The NAM Block

The built-in amp block, served through `InternalBlockFormat` under `format="BlockRig",
identifier="nam"`. It wraps the **existing, verified engine** in `src/dsp/` — none of that code
changes. What changes is the shell around it: the old product was *two* slots with pan/sum
routing baked into the plugin; the block is *one* amp voice, because the lane now owns routing
(want two amps? add two NAM blocks — panned rows arrive with v1.1 splits).

## Processor spec

Mono-in/mono-out DSP on a stereo lane: input = mono per `Input.mode` upstream (lane is stereo;
the block sums L+R × 0.5 to feed the model — or configurably takes L), output duplicated to both
channels. True-stereo/dual-instance mode is a v1.x option (CPU is there: A2 ≈ 3.4%/core).

Signal path (identical to the verified AmpSlot chain):

```
in trim → [gate trigger] → NAM model → DC blocker → output-mode gain → tone stack → [gate gain] → out trim
```

### Parameters (same IDs/ranges/behavior as the shipped ones, minus slot prefixes and routing)

| ID | Range / type | Default | Notes |
|---|---|---|---|
| `in_trim` | −20…+20 dB | 0 | smoothed |
| `out_trim` | −40…+40 dB | 0 | smoothed |
| `gate_on` / `gate_thresh` | bool / −100…0 dB | off / −80 | official-plugin gate constants (now per-block, keyed at block input) |
| `eq_on`, `bass`, `mid`, `treble` | bool, 0–10 | on, 5 | official tone-stack constants |
| `out_mode` | Raw / Normalized / Calibrated | Normalized | official gain math (verified +2.02 dB) |
| `cal_in` / `cal_dbu` | bool / −60…+60 dBu | off / 12.0 | input calibration |
| `slim` | 0…1 | 1.0 | A2 slimmable only; loader-thread applied, debounced |

Dropped from the old product: `pan`, `phase`, `solo`, `mute`, `input_mode`, `master_out`,
`mono_sum` — all now the lane's job. (Phase invert may return as a tiny built-in utility block
later.)

### Model management

Unchanged machinery: `ModelLoader` background thread, staging/retirement swap, prewarm
off-thread, embedded gzipped `.nam` in block state, `dynamic_cast<nam::SlimmableModel*>`
detection, metadata-driven availability (grey out Normalized/Calibrated when absent). The
`WHOLE_ARCHIVE` link requirement for `nam_core` stands.

### Latency

0 at 48 kHz; resampler latency otherwise (27 smp @ 44.1k), reported via the block's
`getLatencySamples()` → chain PDC → outer host. The chain's rebuild-on-latency-change poll picks
up model swaps that change resampling state.

## Inline panel (no floating window)

The panel (13-UI-UX) is the block's editor — the showcase surface of the app:

- Capture strip: model name, gear/tone-type badges from metadata, sample-rate/resampling badge, drag-drop `.nam` target + browser button; recent captures list.
- Big knob row: Input, Bass, Mid, Treble, Output. Second row: Gate (on + threshold), EQ on, Output mode selector, calibration controls (visible when relevant), Model-size slider with breakpoint detents (visible when slimmable).
- Level meters in/out of the block.

## Reuse map (existing → block)

| Existing | Fate |
|---|---|
| `dsp/ResamplingNam`, `ToneStack`, `NoiseGate`, `ModelLoader`, `ResamplerShim` | unchanged |
| `dsp/AmpSlot` | trimmed: routing fields (pan/solo/mute) removed; becomes the block's core |
| `state/Parameters` | rewritten to the single-voice table above (IDs stay stable where kept) |
| `state/PluginState` | reused: gzip model embed/extract helpers |
| `PluginProcessor`/`PluginEditor` (dual-slot) | retired; NAM block gets a slim `AudioProcessor` shell implementing the internal-plugin interface |
| `tests/dsp_tests`, `tests/bench` | unchanged, still gate `src/dsp/` |
| `tests/plugin_tests` | reworked against the NAM block processor + chain engine |

## Acceptance

- A/B against the retired dual-slot build at equal settings nulls (same engine, same constants).
- Rig with a NAM block reopens on a machine without the `.nam` file — identical sound (embedded state).
- Block reports 0 latency at 48 kHz; chain totals update when a 44.1k session forces resampling.
