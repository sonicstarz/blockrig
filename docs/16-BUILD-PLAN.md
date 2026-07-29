# 16 — Build Plan (pivot)

Phases end in verifiable states; run the acceptance check before moving on. UI look-and-feel
work is deliberately *late* — engine correctness first, with a plain-but-functional UI until P5.
The existing NAM tests (`ctest`) must stay green through every phase.

## P0 — De-risk & re-base — **COMPLETE (2026-07-29)**

Findings recorded in 11-RESEARCH §"Verified during P0". Summary: JUCE pinned to **8.0.15**
(not 9.0.0 — new CoreAudio implementation), existing tests green after the bump, VST3 SDK
confirmed MIT in-tree, both AU and VST3 hosting verified against Apple AUs and our own plugin,
**hosting overhead measured at 0.19% of a core per plug-in — the 7× claim is rejected, gate
passed**. Two findings changed downstream plans: `addDefaultFormats()` is deleted in JUCE 8.0.11+
(use `juce::addDefaultFormatsToManager`), and a raw processor chunk is not valid VST3 component
state — child state is opaque and restore fails *silently*, so P4 needs its own verification step.

Artifact: `tests/host_spike.cpp` (throwaway; delete once `host/` lands).

## P1 — Chain engine + NAM block — **COMPLETE (2026-07-29)**

Delivered: `host/BlockChain` (snapshot swap, retirement queue, bypass, latency summation +
`refreshLatency()` poll), `host/BlockInstance` (hosted plug-in + per-block CPU timing + bypass-
parameter preference), `host/InternalBlockFormat`, and `blocks/nam/NamBlockProcessor` (single-voice
NAM served as an `AudioPluginInstance`).

Verified by `tests/chain_tests.cpp` against real plug-ins: insert/reorder/remove, bypass,
mixed AU + VST3 lanes, latency summed correctly (checked against AUDynamicsProcessor's 256
samples), per-block CPU attribution, 30 rounds of live edits during continuous rendering, and the
NAM block loading a capture, reporting 0 latency at 48 kHz, and round-tripping its embedded
capture through state (143.8 KB). Cross-validation: the chain measures the NAM block at 3.59% of
the buffer budget, matching `bench`'s independent 3.54% for the same capture.

**Deviations from plan, deliberate:**
- The old dual-slot product was *not* deleted. Its VST3 is the test subject that `chain_tests` and `host_spike` use to exercise real VST3 hosting, and `plugin_tests` still covers `src/dsp/`. Deletion is deferred to P4, when the host plugin target exists and can replace it as the test subject.
- `dsp/AmpSlot` was reused unchanged rather than trimmed; its `phaseInvert` field is simply left unused (polarity is a lane concern). Touching verified DSP for cosmetic reasons wasn't worth the risk.
- An empty NAM block passes audio through rather than outputting silence — the opposite of the old dual-slot behavior, and correct here: a block with no capture loaded shouldn't mute the rig.

## P2 — Scanning + catalog (scanning half complete 2026-07-29)

- ~~Out-of-process scanner (coordinator/worker via own-executable relaunch), dead-man's pedal, denylist, **60 s watchdog kill**, incremental catalog persistence.~~ Done: `host/PluginCatalog`, `host/PluginScannerWorker`. The watchdog needed a **second half in the child** that the plan did not anticipate — see 11-RESEARCH §P2.3.
- ~~Scan this machine's corpus; document casualties in the denylist.~~ Done: 857 probes → 860 types in ~5.4 min; 3 plugins hang and are denylisted.
- **Deferred to P3, deliberately:** the custom standalone app shell (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`, AudioDeviceManager, `AudioProcessorPlayer`). A standalone app with no lane UI has nothing to show and nothing to drive; it belongs with the UI it exists to host. Scanning did not need it — `tests/scan_tests.cpp` is itself both coordinator and scanner child, which tests the shipping arrangement more honestly than a GUI would.
- **Brought forward from P4:** `BlockRigProcessor` (owns chain + catalog, async block creation, latency reporting) and `state/RigState` (the full schema-1 serializer) were built here, because the scanner needed something to be part of and the schema was already designed. Verified by `tests/rig_tests.cpp`.
- **Done when:** ~~full-corpus scan completes unattended with crashes/hangs contained; catalog survives restart.~~ Met.

## P3 — Functional lane UI (3–5 days)

- Lane view: block tiles, selection, drag-reorder, `+` picker with search/categories/recents, right-click menu, IN/OUT end blocks with channel pickers + meters, error tiles.
- Inline panel: NAM block panel (functional layout, default styling), generic parameter fallback for third-party.
- Floating editor windows with the PatchWork mitigation set (position memory, ESC, close-all, always-on-top, raise-if-open).
- **Done when:** build a rig by mouse alone — guitar in, NAM + two third-party blocks, reorder live, open/close editors — no glitches, no console errors.

## P4 — Rig state + DAW builds (2–4 days)

- Serializer per 14-SCHEMA.md (one code path for `.blockrig` files and DAW chunks), migration scaffold + fixture tests, error-tile state retention round-trip.
- VST3/AU builds of the host; `auval`; latency propagation to the outer host; editor-window behavior inside Logic and Reaper; catalog access from the DAW build; the Logic AU-inside-AU empirical test (11-RESEARCH open item).
- **Done when:** rig saved standalone reopens identically in Logic (AU) and Reaper (VST3), third-party state intact; `auval` passes; missing-plugin rig degrades to error tiles and recovers on reinstall.

## P5 — CPU meter + visual design (3–5 days)

- `CpuMeter` per-block timing, header meter, click-through panel, overload counter.
- Theme pass: LookAndFeel, palette, melatonin_blur, restyled PluginListComponent + picker + panels; first-run flow; settings sheet.
- **Done when:** CPU panel correctly fingers a deliberately-heavy block; every visible surface uses the theme; screenshot test — "doesn't look like a JUCE demo."

## P6 — Hardening & release candidate (ongoing)

- pluginval strictness 10 on VST3; swap-storm + scan-corpus soak tests in CI habit; multi-MB chunk test in Logic autosave; GarageBand best-effort check; crossfade-on-edit polish; dropout counter validation at tiny buffers.

## Deferred (schema-ready, not built)

Parallel rows/splits (v1.1) • snapshots, setlists, A/B (v1.x) • capture creation (archived design)
• true-stereo NAM mode • Windows • additional built-in utility blocks (tuner, IR loader, phase).

## Risk register

| Risk | Mitigation |
|---|---|
| JUCE wrapper hosting overhead real (7× report) | P0 benchmark gate before further investment |
| Hostile plugins in scan corpus | out-of-process + pedal + watchdog, P2 acceptance is a full-corpus scan |
| AU enumeration inside Logic sandbox | P4 empirical test; PatchWork/Unify precedent says AUv2 works |
| DAWs ignore dynamic latency changes | internal PDC always correct; document DAW apply-on-stop behavior |
| Chunk size in DAW projects | embed-state warning >20 MB; Logic autosave test in P6 |
| Scope creep toward node canvas | stages schema + explicit non-goal; revisit only post-v1 |
