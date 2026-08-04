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

## P7 — Live verification round (needs a human at the machine)

Nothing here is new code; it is closing loops that only hardware and consent
dialogs can close. Do this before building further — every later phase stacks on
these paths.

- macOS consent: Allow **microphone** and **Downloads folder** prompts (both re-appear
  after rebuilds because ad-hoc re-signing resets TCC).
- Watcher: drop any new `.nam` into ~/Downloads → appears in Library under *Imported*
  within ~10 s; a `.zip` capture pack becomes a subfolder.
- TONE3000 API: create a free publishable key at tone3000.com/api, paste into
  Settings → Browse TONE3000, sign in, run one search, download one capture.
  **The client tolerates several response shapes but is unverified against the real
  API** (`src/net/Tone3000Client.cpp` — search parsing, `model_url` vs `/models`
  resolution). Expect one round of shape fixes; log raw bodies on mismatch.
- MIDI: map a CC to Mute via Settings → MIDI mappings, verify learn + fire; send a
  program change, verify snapshot recall + strip highlight catching up via the
  MainView timer sync.
- BPM/tap: confirm live (log line carries `bpm` now); if dead, AudioStatus.log
  distinguishes "taps not arriving" from "bpm not applied".
- **DAW pass (old P4 remainder)**: BlockRigPlugin in Logic (AU) and Reaper (VST3):
  rig chunk round-trip, third-party state intact, `auval -v aufx Brg1 Brig` re-run,
  AU-inside-AU empirical answer, catalog access from sandbox, window behaviour
  (in-app windows inside a DAW editor — expect sizing quirks; the editor is resizable
  and MainView hides device-owned affordances when `deviceManager == nullptr`).

**Done when:** every bullet has a yes/no answer written back into this file.

## P8 — Built-in utility blocks + error tiles — **BUILT (2026-07-30, `07f1a2f`)**

Delivered: `blocks/ir/` (IR block + `IrLibrary`), `blocks/eq/`, `blocks/utility/`, metronome in
`host/Transport`, error tiles in `ui/LaneView`. Acceptance ("a rig of NAM → IR → EQ → utility runs
with zero third-party plugins") is verified by build and tests; the degrade/reinstate cycle wants
a live pass in P7.

Extend `host/InternalBlockFormat` (pattern: `blocks/nam/NamBlockProcessor` is the
reference internal plugin — `AudioPluginInstance` subclass, APVTS parameters,
`fillInPluginDescription`, registered in the format's type list).

- **IR loader block** (`blocks/ir/`): stereo convolution via `juce::dsp::Convolution`
  (non-uniform partitioned, zero-latency mode), .wav/.aiff IRs, an IR library that
  mirrors `CaptureLibrary` (folder = `…/BlockRig/IRs`, content-hash dedupe, subfolder
  submenus, Downloads-watcher extension for `.wav` **only when zips look like IR
  packs — do not import every wav in Downloads**, gate on a keyword or ask).
  Category `cabinet`, `WidthNeutralProcessor` (same IR per side; a stereo-IR mode can
  come later). Mark latency if the convolver reports any.
- **Gain/Pan/Phase utility** (`blocks/utility/`): gain −60…+24 dB smoothed, pan law
  −3 dB, phase invert per channel. Trivial DSP; the point is not needing a paid
  plugin for a trim. Width-neutral unless panned.
- **Simple EQ block**: 6-band (HPF, LS, 2×bell, HS, LPF) on `juce::dsp::IIR`.
  Width-neutral.
- **Metronome**: not a block — lives in the Transport. Click generated post-chain,
  pre-mute, level control in the transport bar popover; uses the existing ppq clock.
- **Error tiles (old task #15)**: restore keeps missing blocks' uid+state; give the
  lane a real tile (category `other`, warning glyph, plugin name, "missing — rescan
  or reinstall"), block slot preserved in `BlockChain` as a stub `BlockInstance`
  with no plugin that passes audio through. On successful rescan, offer re-resolve.

**Done when:** a rig of NAM → IR → EQ → utility runs with zero third-party plugins;
deleting a plugin from disk degrades its tile instead of silently dropping it, and
reinstalling restores it with state.

## P9 — Editing quality of life — **BUILT (2026-07-30, `207f364`)**

Delivered: `state/UndoHistory`, `state/BlockFavorites`, clipboard copy/paste-after in
`ui/LaneView`, and per-snapshot "Edit what's saved…" in `ui/SnapshotStrip`.

- **Undo/redo**: ring buffer of serialized rigs (`rigstate::toValueTree` — already
  cheap enough that the dirty checker runs it). Snapshot the tree before every
  structural edit and on a debounce after parameter storms; ⌘Z/⇧⌘Z. Restoring goes
  through the same `SequentialRestore` path as file load; reuse
  `onBlockAboutToBeRemoved` so windows close correctly. **Risk:** full-restore undo
  reinstantiates plugins (slow for Waves); mitigate by diffing block sets and only
  rebuilding changed blocks — measure first, optimise only if >200 ms.
- **Block favorites**: named plugin-state chunks, exactly the CaptureLibrary pattern
  (`…/BlockRig/Favorites/<plugin>/<name>.blockfav` = {description + state chunk}).
  Save from block right-click; new "Favorites" section in BlockPicker seeded from it.
- **Copy/paste block**: right-click copy → paste-after target; carries state chunk.
  Internal clipboard is fine; system clipboard as base64 is a free bonus for sharing.
- **Per-snapshot safes editing**: reopen the AddPanel pre-populated for an existing
  snapshot (right-click → "Edit what's saved…").

**Done when:** ⌘Z survives add/remove/move/param-change round-trips with windows
open; a favorited Valhalla preset drops into a different rig with settings intact.

## P10 — Stage story: setlists, gig view, MIDI phase 2 — **BUILT (2026-07-30, `492501b`)**

Delivered: `state/Setlist`, `ui/GigView`, MIDI clock follow (`MidiEngine::setFollowMidiClock`),
per-mapping channel filters and expression ranges. The acceptance test is a hands-free two-song
set from a floor controller — **hardware, so it belongs to P7.**

- **Setlists**: ordered lists of rig files (`…/BlockRig/Setlists/*.blockset`, JSON
  array of rig paths + names). Home screen gains a Setlists tab; the rig header
  arrows follow the active setlist order when one is loaded (falls back to folder
  order otherwise — `MainView::listRigs` is the single point to change).
- **Gig view**: full-screen performance surface over MainView (toggle in header or
  MIDI-mappable): giant snapshot buttons, current rig name, next/prev rig, tuner and
  tempo readouts, nothing editable. Dark-stage contrast, ≥72 pt hit targets.
- **MIDI phase 2**: MIDI clock in → transport bpm (average intervals like tap;
  Transport already follows-host, reuse that path with a "following clock" flag);
  per-mapping channel filter; global setting "program change targets: snapshots |
  rigs"; expression range scaling (min/max per mapping); MIDI activity indicator in
  the MIDI panel (last CC seen — makes "is my pedal even arriving" self-answering).

**Done when:** a two-song set runs hands-free from a floor controller: PC changes
rigs (or snapshots per setting), expression rides a mapped parameter, gig view
readable from standing height.

## P11 — Spillover (the flagship, hardest last) — **PARTLY BUILT (2026-07-30, `837c04e`)**

**Built:** snapshot ducking (`state/Snapshots.cpp` — the dip falls while states land beneath it),
and per-block tail spillover in `BlockChain` — a removed block moves to one of 12 `TailSlot`s and
is silence-fed for `mTailCarrySeconds` (4 s) with a linear fade, freed only after the audio
thread clears the slot.

**Deliberate compromise, documented in `BlockChain.h`:** retired blocks render *alone*, so a delay
that fed an amp tails out dry. Rendering the retired graph with topology intact would double-
process instances shared with the new snapshot and corrupt their state.

**Not built:** rig-switch spillover — the outgoing chain does not keep rendering into a tail bus
across a rig change (`MainView::loadRigFile` swaps directly). The acceptance criterion below
therefore stands half-met: snapshot tails ring, rig-change tails do not. The audible check at 128
samples is a P7 item either way.

Goal: switching snapshots or rigs lets delay/reverb tails ring out instead of
cutting. This is the Helix-Stadium-headline feature; GP calls it Patch Persist.

- **Snapshot spillover** (cheaper): setState in place already preserves most tails;
  the click risk is plugins that reset on setState. Add a short output crossfade
  (~30 ms, equal-power) around snapshot apply at the processor level. Measure which
  of the user's plugins click before engineering more.
- **Rig spillover** (real work): on switch, the outgoing chain keeps rendering into
  a tail bus for N seconds (fixed 4 s default), mixed post-chain pre-mute. Requires
  two live chains → the retirement machinery already keeps old snapshots alive;
  extend `BlockChain` so a retired snapshot can keep processing silence-fed until
  its tail window ends, THEN retires. CPU doubles during overlap — surface that in
  the CPU meter honestly. Blocks shared between old and new rig (same plugin
  instance) cannot be in both graphs: rig switch rebuilds instances anyway, so the
  overlap is between old instances (dying) and new ones (loading) — which also masks
  load time. **Verify-first:** measure instantiation gap per plugin; if loading
  dominates, spillover doubles as seamless-switching and is worth more.

**Done when:** a dotted-eighth tail audibly rings across a snapshot change and a
rig change at 128-sample buffers without dropouts on the dev machine.

## P12 — Ship

- pluginval strictness 10; swap-storm and scan-corpus soaks; tiny-buffer dropout
  validation; Logic autosave multi-MB chunk test (P6 list, unchanged).
- **Packaging**: Developer ID signing + notarization (TCC re-prompt problem
  disappears once the signature is stable — worth doing EARLY in this phase for
  everyone's sanity), app icon, DMG, versioned releases, a README for musicians.
- First-run flow: scan prompt wording, mic/Downloads permission pre-explanation
  screen before macOS asks (double-prompt pattern), rigs folder seeding with one
  demo rig using only built-in blocks.

## Process notes for whoever executes this (read once)

- `ctest --test-dir build` green before claiming any phase; `auval` after plugin
  changes; commit per phase with the story in the message.
- Diagnose in the live path, not the harness — the harness has no audio thread, no
  TCC, no session restore, and it exonerated three real bugs in a row here.
  `--chain-check session` + AudioStatus.log are the ground-truth instruments; keep
  them working as features move.
- Rebuilds re-trigger macOS consent prompts until P12 signing lands; a "frozen" app
  right after launch is almost always a dialog, not a hang.
- The width/mono model: blocks fed one channel negotiate mono-in; `WidthNeutral`
  built-ins pass mono-ness through; renegotiation must run under `suspendAudio`.
  Every new built-in must decide its width behaviour explicitly.
- UI follows engine state via the MainView timer (snapshots, tuner) — extend that
  pattern for anything MIDI or automation can change behind the UI's back.

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
