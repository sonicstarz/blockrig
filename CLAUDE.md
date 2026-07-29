# CLAUDE.md — NAM Modeler

Dual-capture stereo NAM plugin (VST3/AU/Standalone, JUCE + NeuralAmpModelerCore). **Currently in
planning phase — read `docs/` before writing any code.**

## Read first, in order

1. `docs/05-BUILD-PLAN.md` — what to build next (phased milestones with acceptance criteria)
2. `docs/02-ARCHITECTURE.md` — how (signal flow, threading, RT-safety rules, state format)
3. `docs/03-PARAMETERS.md` — exact parameter IDs/ranges/defaults (IDs freeze at first release)
4. `docs/01-RESEARCH.md` — the facts behind decisions + **VERIFY items to resolve in M0**
5. `docs/04-CAPTURE.md` — capture-wizard design (M5+ only)

## Hard rules

- **Audio thread**: no allocation, locks, file I/O, JSON parsing, logging, model destruction, `Reset()`, `prewarm()`, or `SetSlimmableSize()`. Model swaps go through the staging/retirement pattern in 02-ARCHITECTURE.
- `nam::DSP::GetLoudness()` **throws** if the model lacks loudness — always check `HasLoudness()` first (same for input/output level).
- Prewarm off the audio thread at load time, never in `processBlock`.
- Don't reimplement NAM training in C++ — training is the bundled Python helper (04-CAPTURE). Don't fork the trainer; pin and invoke it.
- Match official-plugin DSP constants exactly (tone stack, gate, DC blocker, output-mode math — listed in 01-RESEARCH §4) so captures A/B identically.
- Parameter IDs and choice-parameter index order are frozen once released — additive changes only.
- Milestone acceptance checks in 05-BUILD-PLAN are the definition of done — run them, don't skip to the next milestone.

## Stack

JUCE 8/9 (CMake, `juce_add_plugin`), C++20, NeuralAmpModelerCore ≥ v0.5.4 as submodule with
`NAM_ENABLE_A2_FAST=ON`, AudioDSPTools for ResamplingContainer/gate. Core uses `double` samples
(convert at slot boundary) and nlohmann/json in its public API. All licenses permissive — see
README licensing table.

## Verify before relying on (flagged during 2026-07 research)

- Exact `"architecture"` strings for A2/container models in `.nam` files (read Core source)
- "A2 Lite/Full" vs "nano/standard" naming
- MPS training wall-clock on this machine (M0 experiment)
- TONE3000 upload API existence (assume none; link-out flow)
