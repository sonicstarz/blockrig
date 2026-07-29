# CLAUDE.md — BlockRig (working title)

Block-based rig host (standalone + VST3/AU) hosting third-party VST3/AU blocks plus a built-in
NAM amp block. **Pivoted 2026-07-29; design done, host code not started.** The repo currently
contains the previous incarnation (dual-slot NAM plugin, working, tests green) whose `src/dsp/`
engine is carried forward as the NAM block.

## Read first, in order

1. `docs/16-BUILD-PLAN.md` — what to build next (P0–P6, acceptance criteria)
2. `docs/12-ARCHITECTURE.md` — chain engine, scanning, threading, deployment
3. `docs/14-SCHEMA.md` — normative rig state schema (versioned; don't improvise fields)
4. `docs/15-NAM-BLOCK.md` — NAM block spec + exact reuse map of existing code
5. `docs/13-UI-UX.md` / `docs/11-RESEARCH.md` — UI decisions / research basis
6. `docs/archive-nam-plugin/01-RESEARCH.md` §"Verified during M0" — hard-won NAM facts

## Hard rules

- **Audio thread**: no allocation, locks, file I/O, JSON parsing, logging, or destruction. All chain edits build snapshots on the message thread; swap via atomic pointer; free via retirement queue. (Pattern already proven in `src/dsp/AmpSlot`.)
- `nam_core` links with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` — otherwise every model fails to load ("No config parser registered"). Never "clean up" this line.
- A2 detection is `dynamic_cast<nam::SlimmableModel*>`, never architecture-name matching.
- `GetLoudness()`/`GetInputLevel()`/`GetOutputLevel()` throw when metadata is absent — check `Has*()` first.
- Plugin scanning is **out-of-process only** (+ dead-man's pedal + denylist + 60 s watchdog). Never scan in-process outside a debug flag.
- No VST2 hosting (licensing). Element source is GPL — read, never copy.
- Schema changes follow `docs/14-SCHEMA.md` migration policy; `schemaVersion` bumps only on breaking shape changes, with fixture-tested migrations.
- Existing `dsp_tests`/`bench` stay green through every phase; run `ctest --test-dir build` before claiming any phase done.
- Match official NAM plugin DSP constants exactly (tone stack, gate, DC blocker, output-mode math) — they're what makes captures sound "right".

## Stack

JUCE 8.0.x (bump past 8.0.9 in P0 — VST3 hosting regression) + CMake, C++20;
NeuralAmpModelerCore v0.5.4 (`NAM_ENABLE_A2_FAST=ON`); hosting via `JUCE_PLUGINHOST_VST3=1` /
`JUCE_PLUGINHOST_AU=1`; custom standalone shell (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`);
melatonin_blur/_inspector for UI. `extras/AudioPluginHost` in the JUCE tree is the reference for
scanning, InternalPluginFormat, and PluginWindow patterns.

## Verify before relying on (P0 checklist, from research)

- JUCE version's VST3 hosting status + `juce_audio_processors_headless` module split
- VST3 SDK MIT text in the bundled SDK
- Hosted-plugin CPU overhead benchmark (unconfirmed 7× report — gate on this)
- AU instantiation inside Logic; settings-file paths under DAW sandboxes
