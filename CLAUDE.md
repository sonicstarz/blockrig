# CLAUDE.md — BlockRig (working title)

Block-based rig host (standalone + VST3/AU) hosting third-party VST3/AU blocks plus a built-in
NAM amp block. **v1 feature set is built (P0–P11) and tests are green.** Outstanding: P6
hardening, **P7 live verification (needs a human at the machine)**, and P12 ship. `src/dsp/` is
the previous incarnation's engine, carried forward as the NAM block.

Next planned work is `docs/19-GRAPH-ENGINE.md` — free-form routing on a snapping patch canvas,
superseding the lane/stage model. Planned 2026-08-02, no code yet.

## Read first, in order

1. `docs/16-BUILD-PLAN.md` — phase status and what's left (P6, P7, P12); acceptance criteria
2. `docs/19-GRAPH-ENGINE.md` — the next architecture, if that's what you're here for
3. `docs/12-ARCHITECTURE.md` — chain engine, scanning, threading, deployment
4. `docs/14-SCHEMA.md` — normative rig state schema (versioned; don't improvise fields)
5. `docs/15-NAM-BLOCK.md` — NAM block spec + exact reuse map of existing code
6. `docs/13-UI-UX.md` / `docs/18-UI-OVERHAUL.md` / `docs/11-RESEARCH.md` — UI decisions / research
7. `docs/archive-nam-plugin/01-RESEARCH.md` §"Verified during M0" — hard-won NAM facts

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

JUCE pinned to **8.0.15** (not 9.0.0 — new CoreAudio implementation) + CMake, C++20;
NeuralAmpModelerCore v0.5.4 (`NAM_ENABLE_A2_FAST=ON`); hosting via `JUCE_PLUGINHOST_VST3=1` /
`JUCE_PLUGINHOST_AU=1`; custom standalone shell (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`);
melatonin_blur/_inspector for UI. `extras/AudioPluginHost` in the JUCE tree is the reference for
scanning, InternalPluginFormat, and PluginWindow patterns.

## Still unverified (P7 — needs a human at the machine)

P0's checklist is closed: hosting overhead measured at 0.19% of a core per plug-in (the 7× claim
is rejected), VST3 SDK confirmed MIT in-tree, AU and VST3 hosting both verified. What remains
open, per `docs/16-BUILD-PLAN.md` §P7:

- macOS mic + Downloads consent flow (TCC re-prompts after every ad-hoc re-sign)
- TONE3000 API response shapes — `src/net/Tone3000Client.cpp` is written against docs, not the
  live API; expect one round of shape fixes
- MIDI learn/PC/expression and BPM tap against real hardware
- DAW pass: AU in Logic, VST3 in Reaper — chunk round-trip, AU-inside-AU, sandbox catalog access

## Diagnose in the live path, not the harness

The test harness has no audio thread, no TCC, and no session restore — it exonerated three real
bugs in a row. `--chain-check session` and AudioStatus.log are the ground-truth instruments;
keep them working as features move.
