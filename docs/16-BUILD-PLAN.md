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

## P3 — Functional lane UI — **COMPLETE (2026-07-29)**

Delivered `ui/`: `Theme` (all colours/metrics in one file), `LaneView` with `BlockTile` and
`EndBlock`, `BlockPicker` (search-first), `PluginEditorWindows` (full PatchWork mitigation set),
`NamBlockPanel` (inline editor for the built-in amp), `CpuMeter` (header + per-block breakdown),
`MainView`, and `HostedEditor`. Plus the two deployment targets: `BlockRigApp`
(`juce_add_gui_app`) and `BlockRigPlugin` (VST3 + AU). BlockRig AU passes `auval`; all four fast
tests still pass.

**Deviations from plan:**
- The standalone is a plain `juce_add_gui_app`, **not** `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` as 12-ARCHITECTURE proposed. Fighting `StandaloneFilterWindow` bought nothing: `main()` must be able to become a scanner child before any UI exists, and the app needs its own `AudioDeviceManager` anyway. Two thin targets over shared sources is simpler than one target with a special mode.
- `createEditor()` lives in `ui/HostedEditor.cpp`, with `tests/headless_editor_stub.cpp` for headless targets, so `rig_tests` can link the processor without the whole interface.
- The CPU meter (planned for P5) landed here, since the lane already needed per-block load for its activity strips.
- **Not yet done in the UI:** the drop indicator is tracked but not drawn; error tiles for missing plugins exist in the schema and restore path but have no dedicated tile rendering yet; the settings sheet is a popup menu rather than a proper panel, and "Rescan plug-ins" currently explains itself rather than running a scan.

## P4 — Rig state + DAW builds — **mostly complete (2026-07-30)**

- ~~Serializer per 14-SCHEMA.md (one code path for `.blockrig` files and DAW chunks)~~ Done:
  `state/RigState` + `state/RigFiles`, `.blockrig` save/open from the Rig menu, session autosave
  every 30 s + restore on launch, sequential block rebuild with per-plugin state verification.
- **Remaining:** the in-DAW verification pass (Logic AU, Reaper VST3, `auval` re-run after the
  window-UI changes, AU-inside-AU test) and error tiles for missing plugins — the schema and
  restore path retain them, the lane does not draw them yet.

## P5 — CPU meter + visual design — **mostly complete (2026-07-30)**

- Done: `CpuMeter` (header + per-block), full theme/LookAndFeel, block categories with drawn
  icons, Cortex-style compact lane with branch/rejoin connectors, window-based block UI with
  pinning + dimmed backdrop, header meters with numeric dBFS and a live WIDTH readout, NAM
  faceplate panel, tempo/tap/time-signature transport.
- **Remaining:** first-run flow; drop indicator during tile drag.

## P6 — Hardening & release candidate (ongoing)

- pluginval strictness 10 on VST3; swap-storm + scan-corpus soak tests in CI habit; multi-MB chunk test in Logic autosave; GarageBand best-effort check; crossfade-on-edit polish; dropout counter validation at tiny buffers.

## Deferred (schema-ready, not built)

Snapshots, setlists, A/B (v1.x) • capture creation (archived design) • Windows • additional
built-in utility blocks (tuner, IR loader, phase). *(Splits and true-stereo NAM, deferred here
originally, were built in v1: dual-mono/parallel stages and a dual-instance NAM block.)*

## Hard-won facts (2026-07-29/30, keep)

- Blocks fed a single channel must negotiate **mono in / stereo out**; stereo/stereo fed two
  identical channels makes ping-pong/widening plug-ins symmetric and permanently centred.
- Re-negotiating a live block races the audio thread — `BlockChain::suspendAudio` (wired to
  `suspendProcessing`) must wrap any in-place re-prepare. The harness has no audio thread and
  will never catch this class of bug.
- `setStateInformation` can rearrange a VST3's buses after negotiation; `layoutDrifted()` +
  post-restore `prepareLane` handle it.
- Editors must close before their plug-in dies (`onBlockAboutToBeRemoved`), or their timers fire
  into freed memory.
- macOS grants mic access asynchronously: device state logged at startup is meaningless; every
  ad-hoc re-sign can re-prompt.
- Diagnostics: `--scan`, `--audio-check`, `--plugin-check <name>`, `--chain-check <a,b,...>` /
  `--chain-check session` (per-block width, negotiated layouts).

## Risk register

| Risk | Mitigation |
|---|---|
| JUCE wrapper hosting overhead real (7× report) | P0 benchmark gate before further investment |
| Hostile plugins in scan corpus | out-of-process + pedal + watchdog, P2 acceptance is a full-corpus scan |
| AU enumeration inside Logic sandbox | P4 empirical test; PatchWork/Unify precedent says AUv2 works |
| DAWs ignore dynamic latency changes | internal PDC always correct; document DAW apply-on-stop behavior |
| Chunk size in DAW projects | embed-state warning >20 MB; Logic autosave test in P6 |
| Scope creep toward node canvas | stages schema + explicit non-goal; revisit only post-v1 |
