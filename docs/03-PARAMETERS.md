# 03 — Parameter Specification

All parameters live in a `juce::AudioProcessorValueTreeState`. IDs are stable strings (never
reuse/rename once shipped). Per-slot parameters exist twice with `a_`/`b_` prefixes. Ranges and
defaults deliberately match the official NAM plugin where an equivalent exists, so users get
familiar behavior and captures sound identical A/B'd against the official plugin.

## Global parameters

| ID | Name | Type / Range | Default | Notes |
|---|---|---|---|---|
| `input_mode` | Input Mode | choice {Mono, Stereo} | Mono | Mono: L (or single ch) → both slots. Stereo: L→A, R→B. Not automatable-critical, but keep automatable. |
| `master_out` | Master Output | float −40…+40 dB | 0 | Post-sum trim. Smoothed. |
| `mono_sum` | Mono Sum | bool | off | Collapses master out to mono (compat check). |
| `gate_on` | Gate | bool | off | Single gate keyed from the (mono-summed) input, applied per slot pre-model. One gate, not two — both slots see the same instrument. |
| `gate_thresh` | Gate Threshold | float −100…0 dB | −80 | Official-plugin gate constants: time 0.01, ratio 0.1, open 5 ms, hold 10 ms, close 50 ms. −100 = effectively off. |

## Per-slot parameters (×2: prefix `a_` / `b_`)

| ID (slot A shown) | Name | Type / Range | Default | Notes |
|---|---|---|---|---|
| `a_enabled` | Amp A On | bool | on | Off = slot outputs silence (skip NN entirely — real CPU savings). |
| `a_in_trim` | Input Trim | float −20…+20 dB | 0 | Pre-model. Smoothed. Matches official input-level range. |
| `a_out_trim` | Output Trim | float −40…+40 dB | 0 | Post-EQ. Smoothed. |
| `a_pan` | Pan | float −1…+1 | 0 | Constant-power sin/cos (−3 dB center). Smoothed. |
| `a_phase` | Phase Invert | bool | off | Polarity flip post-trim. Essential for dual-amp alignment. |
| `a_solo` | Solo | bool | off | Standard solo logic across the two slots. |
| `a_mute` | Mute | bool | off | |
| `a_eq_on` | EQ | bool | on | True bypass when off (no biquad processing). |
| `a_bass` | Bass | float 0…10 | 5 | 150 Hz biquad, Q 0.707, 4 dB per unit from 5 (±20 dB). |
| `a_mid` | Mid | float 0…10 | 5 | 425 Hz peak, Q 1.5 cut / 0.7 boost, 3 dB/unit (±15 dB). |
| `a_treble` | Treble | float 0…10 | 5 | 1800 Hz biquad, Q 0.707, 2 dB/unit (±10 dB). |

### NAM-exposed parameters (per slot)

These surface everything a `.nam` model actually exposes (see 01-RESEARCH §3):

| ID | Name | Type / Range | Default | Notes |
|---|---|---|---|---|
| `a_out_mode` | Output Mode | choice {Raw, Normalized, Calibrated} | Normalized | Normalized: +(−18 − loudness) dB when model `HasLoudness()`, else acts as Raw. Calibrated: +(outputLevel − `a_cal_dbu`) dB when `HasOutputLevel()`, else Raw. Grey out unavailable modes in UI but keep the parameter's full range for automation stability. |
| `a_cal_in` | Calibrate Input | bool | off | When on and model `HasInputLevel()`: apply gain aligning interface level (`a_cal_dbu`) to the model's `input_level_dbu`. |
| `a_cal_dbu` | Interface Level (dBu) | float −60…+60 dBu | 12.0 | The user's interface send/return level. Used by both Calibrate Input and Calibrated output mode. Same default as official plugin. |
| `a_slim` | Model Size | float 0…1, step 0.01 | 1.0 | A2 slimmable/container models only; hidden otherwise. Applied off-audio-thread (debounced ~100 ms). UI shows detents at `GetSlimmableSizeBreakpoints()` (e.g. Lite/Full). 1.0 = full quality. |

**Non-parameter state** (not automatable, stored in ValueTree, not APVTS): per-slot embedded model
JSON, model path, model display name (see 02-ARCHITECTURE state layout).

## Behavior notes

- **Parameter count:** 5 global + 15×2 per-slot = 35 — modest; a flat APVTS layout is fine. Group them (`juce::AudioProcessorParameterGroup`) as "Amp A" / "Amp B" / "Global" for hosts that display groups.
- **dB parameters** use `juce::NormalisableRange` with symmetric skew so 0 dB sits at slider center; display one decimal + "dB".
- **Choice parameters** must keep index order stable forever (host automation stores indices).
- **Smoothing**: all gains, pan — ~20 ms linear-in-dB (gain) / linear (pan). Phase/solo/mute/enable click-free via short ramp (5 ms).
- **Tone-stack knobs** map 0–10 like real amp knobs; recompute biquad coefficients on change per block.
- **What happens on model load** with respect to parameters: nothing resets. Trims/EQ/pan are the user's mix decisions and persist across model swaps (matches official plugin).
- **Latency reporting** is not a parameter but changes with model SR mismatch — see 02-ARCHITECTURE.
