# 11 — Research Findings (pivot)

Condensed from web research conducted 2026-07-29 for the block-host pivot. Facts here drive
12-ARCHITECTURE and 13-UI-UX. Items marked **VERIFY** need re-checking at build time.
NAM-engine findings from the first incarnation remain valid — see
`archive-nam-plugin/01-RESEARCH.md`, especially its "Verified during M0" section.

## 1. JUCE hosting machinery (JUCE 8, verified against master source)

- Hosting lives in `juce_audio_processors`: `AudioPluginFormatManager` (+`addDefaultFormats()`), `VST3PluginFormat`, `AudioUnitPluginFormat`, `KnownPluginList`, `PluginDirectoryScanner`, `PluginListComponent` (ready-made scan/manage UI). Enable with `JUCE_PLUGINHOST_VST3=1` and `JUCE_PLUGINHOST_AU=1` — identical for standalone and plugin builds; nothing extra per deployment.
- `createPluginInstanceAsync` is **mandatory for AUv3** (sandboxed, out-of-process, message-thread instantiation); sync creation works for VST3/AUv2. Use async everywhere for uniformity.
- **JUCE ships a complete out-of-process scanning reference** in `extras/AudioPluginHost`: `ChildProcessCoordinator`/`ChildProcessWorker` relaunching the app's own executable with a scanner UID, `KnownPluginList::setCustomScanner()`, dead-man's-pedal file (`applyBlacklistingsFromDeadMansPedal`), denylist stored in `KnownPluginList`. Gap: it survives *crashes* but not *hangs* — add a watchdog that kills the child after a timeout (Element uses 50 s; iZotope Trash and Harrison Microglide are known scan-hangers).
- `extras/AudioPluginHost/Source/Plugins/InternalPlugins.*` shows the **`InternalPluginFormat` pattern** — exactly how to register built-in processors (our NAM block) as first-class citizens next to scanned plugins.
- `UI/PluginWindow.h` in the same example is the canonical child-editor window manager (normal/generic-parameter fallback modes, per-plugin position memory).
- **JUCE 8.0.11 moved `AudioProcessorGraph`** to a new `juce_audio_processors_headless` module. **VERIFY** module layout against whatever JUCE version we pin (we currently vendor 8.0.9 — see also the 8.0.9 VST3-hosting regression below).
- Known 8.0.x hosting bugs to watch: 8.0.9 had a VST3 host-side loading regression (ID mismatch in `findClassMatchingDescription`) — **VERIFY and likely bump JUCE past 8.0.9 before building the host**; hosted-editor window sizing constraints reported on 8.0.12; a macOS crash on right-clicking hosted VST3 parameters; one unconfirmed report of ~7× CPU overhead hosting VST3 through JUCE's wrapper.

## 2. AudioProcessorGraph — evaluated and rejected for v1

Read from current master source:

- Topology edits rebuild a `RenderSequence` on the **message thread**, installed via spinlock try-lock pointer-swap at the top of `processBlock`, retired sequence freed by a timer. (Good pattern — we already use its equivalent in `AmpSlot`.)
- But: every node's process op takes the hosted processor's `CriticalSection` callback lock per block; rendering is single-threaded; if prepare-settings change, the graph **outputs silence** until the rebuild lands; a node calling `setLatencySamples` mid-flight does *not* trigger recompensation (must call `graph.rebuild()`); and there is an **open, unfixed PDC bug** (June 2025, forum #66385) with multi-destination connections of differing delays.
- It does auto-compensate parallel paths (inserts `DelayChannelOp`s) and propagates total latency to the graph-as-processor.

**Decision: custom chain engine** (12-ARCHITECTURE). Our topology is a lane of stages, at most a
structured 2-row split later — we don't need arbitrary-graph machinery, and rolling the chain
gives us trivially correct per-block CPU timing, no per-node locks, no silence gaps, and PDC we
control (a delay line on the shorter row). `tracktion_graph` exists (multithreaded,
sample-accurate) if we ever outgrow this; keep `AudioPluginInstance` as the block interface so
the engine stays swappable.

## 3. Hosting plugins inside a plugin

- **Commercially proven**: Blue Cat PatchWork, DDMF Metaplugin, Unify, Element all host VST3/AU from inside VST3/AU/AAX shells. AUv2 instantiation in-process inside another host works.
- **Sandboxed outer hosts are the hazard.** GarageBand sandboxes plugins' filesystem access — Unify's known-plugins list, stored at a normal user path, appeared empty inside GarageBand while fine standalone. Logic loads AUv3 out-of-process; behavior of AU enumeration inside such sandboxes is undocumented. Policy: **support Logic (AUv2 in-process), treat GarageBand/AUv3-sandboxed contexts as best-effort**, and store settings/plugin lists where a sandboxed process can reach them (test explicitly).
- **Latency to the outer host**: `setLatencySamples` triggers VST3 `restartComponent(kLatencyChanged)` / AU property change, but DAWs commonly apply it only on transport stop. Ship internal PDC regardless (PatchWork/Metaplugin precedent) and document the DAW behavior.
- **State size**: no spec'd chunk limit in VST3/AU; folklore risk above ~10–50 MB (old GarageBand crashes, autosave bloat). Store child-plugin state inline (base64), store big payloads (IRs, sample libraries) by reference — the embedded `.nam` at 50–300 KB gzipped is fine. **VERIFY** Logic autosave behavior with multi-MB chunks once real rigs exist.
- VST3 child-editor repaint issues on Windows are documented — irrelevant until the Windows port.

## 4. Licensing (all clear)

- **Steinberg relicensed the VST3 SDK to MIT** (Nov 2025). Hosting VST3 requires only MIT attribution. **VERIFY** the SDK copy bundled in our pinned JUCE carries the MIT text.
- AU hosting = Apple system API, no SDK license. **VST2 remains closed to new licensees — skip.**
- JUCE terms unchanged (Starter free < $20k revenue / AGPLv3 / Indie).
- melatonin_blur, melatonin_inspector: MIT. Kushview Element is **GPL — read for ideas, never copy code**.

## 5. CPU metering

- Every serious DAW meter shows **% of audio-callback budget** (`timeToProcessBlock / (blockSize/sampleRate)`), not OS CPU. 100% = dropout. The #1 user confusion in every forum thread is this-vs-Activity-Monitor — document it in-app.
- `AudioDeviceManager::getCpuUsage()` gives exactly this, but only standalone (we own the device). Inside a DAW: time our own `processBlock`. **JUCE has no per-plugin timing utility** — wrap each block's process call with a steady clock; publish via `std::atomic<float>` relaxed stores; CAS-max loop for peaks; EMA smoothing + slower-decay peak-hold (~500 ms) for display; show average *and* peak (averages hide the spikes that cause dropouts — Reaper's "longest block" insight).
- Reaper's per-FX Performance Meter is the gold standard; Gig Performer *doesn't* actually ship per-plugin CPU and its users keep asking — evidence of demand for our per-block panel.

## 6. UX prior art (drives 13-UI-UX)

- **Lane wins, canvas loses** for guitarists. Praised-for-UX products all use a horizontal drag-to-reorder chain: Helix Native (the canonical model — Input/Output as literal end blocks, split paths appear by dragging a block below the line), GENOME 2 (12-slot dual lanes, categorized left-click add menu, right-click bypass/replace/delete, per-insert trim, **CPU meter in the header**, Codex block loads NAM files), BIAS FX 2, Tonocracy (chain-first but unlimited nesting), AmpliTube 5 (chain strip praised, overall UX panned). Free canvases (Metaplugin, Element, Carla patchbay, Gig Performer wiring) serve power users, not guitarists. Guitar Rig 7 *added* a linear chain sidebar to its rack — convergent evolution toward the strip.
- Neural DSP's fixed chain is the acknowledged limitation reviewers cite ("no way to reorder") — our reorderable lane beats it; their *aesthetic* is the benchmark.
- **Third-party editors: floating windows everywhere** (PatchWork, GP, Unify, MainStage); only Element embeds editors in-canvas. Window sprawl is a real complaint — copy PatchWork's mitigations wholesale: per-preset position restore, always-on-top option, ESC closes frontmost, show/hide from the block, close-all command.
- Built-in blocks edit **inline in a bottom panel** (Neural DSP/GENOME pattern: lane on top, selected block's controls below).
- I/O selection: device/buffer in a settings sheet, but channel pick + gain + live meters on the **Input/Output end blocks themselves** (Helix flanks its signal view with I/O meters; guitarists need "Input 1 mono" without hunting).
- Preset patterns worth copying later: single preset pool + setlists-as-pointers (Helix Stadium), snapshots over a loaded chain, FabFilter A/B+Copy, searchable browser with favorites. Glitch-free preset switching (Gig Performer's rackspace preloading) should shape the schema even though it ships later.
- Visual toolchain: custom `LookAndFeel_V4` subclass + **melatonin_blur** (fast shadows = modern depth), **melatonin_inspector** (dev-time Figma-style inspector), Pamplejuce as CI reference. Mood-board: GENOME, Tonocracy, Quad Cortex ("a triumph of design" — Guitar World).

## 7. This machine (test corpus)

58 VST3s + 67 AU components installed (Plugin Alliance, ASAF, ADPTR, etc.) — realistic scan
corpus, guaranteed to include at least one misbehaver. `bx_rockrack V3 Player` is a guitar amp
plugin — good third-party block for demos.

## Verified during P0 (2026-07-29, JUCE 8.0.15, Apple Silicon)

Measured by `tests/host_spike.cpp`. These supersede the assumptions above where they differ.

1. **JUCE pinned to 8.0.15.** Chosen over 9.0.0, which ships a brand-new macOS CoreAudio implementation — not something to adopt days after release in an app that owns the audio device. The existing NAM plugin builds and all tests pass on 8.0.15 unchanged.
2. **VST3 SDK is MIT confirmed in-tree**: `modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` reads "MIT License, Copyright (c) 2025, Steinberg Media Technologies GmbH". Note the path — the SDK moved into the headless module.
3. **API break from the module split**: `AudioPluginFormatManager::addDefaultFormats()` is `= delete` in JUCE 8.0.11+. Use the free function `juce::addDefaultFormatsToManager(manager)` (GUI build) or `addHeadlessDefaultFormatsToManager(manager)`. `juce_audio_processors` depends on `juce_audio_processors_headless`, so linking the former is still all we need.
4. **Hosting works for both formats.** `AudioUnit` and `VST3` both compiled in and enumerated; the AU format reports **797 installed plug-ins** on this machine. Apple's AUBandpass / AUDynamicsProcessor / AUDelay and our own NAM Modeler VST3 all instantiated, rendered finite audio, and reported latency correctly (AUDynamicsProcessor: 256 samples — proves latency propagation will have real values to sum).
5. **The 7× overhead claim is not reproduced.** Fixed per-plugin hosting cost measured at **0.19% of one core** for our (idle) VST3 and **0.07–0.10%** for Apple's AUs. There is no mechanism for a multiplier — `processBlock` is called once per block either way — and the in-process NAM DSP cost (A2 3.77%, A1 7.06%) matches the `bench` reference (3.54% / 8.51%). A 10-block chain therefore pays roughly 1–2% of a core in wrapper overhead. **Gate passed; proceed.**
6. **A raw processor chunk is NOT valid VST3 component state.** JUCE's plug-in-side wrapper nests the processor's chunk inside its own container, so a hand-built or foreign-sourced chunk is silently ignored — measured: 148 KB pushed in, 2 KB of defaults read back, no error raised. **Consequences for P4:** child state must always be obtained from the hosted instance's own `getStateInformation` and handed back to `setStateInformation` **on the same plug-in type**; never synthesize or edit child chunks; and a failed restore is silent, so the rig loader needs its own sanity check (e.g. verify a known parameter after restore) rather than trusting the call.
7. **Hosted VST3 parameter changes only land while audio is flowing.** `setValueNotifyingHost` followed immediately by `getStateInformation` captures the *old* value; the change reaches the processor via the audio callback. Any test or preset code that sets parameters then saves must render a few blocks first. (This also means preset application in a stopped standalone needs a few silent blocks pumped, or state-level application instead of parameter-level.)

## Verified during P2 (2026-07-29, this machine's real corpus)

Measured by `tests/scan_tests.cpp`, which acts as both coordinator and scanner child.

1. **The corpus is bigger than the folder listing suggests.** 797 AU identifiers + 60 VST3 files = **857 probes yielding 860 plug-in types** (AU components and VST3 shells each contain several plug-ins). A full out-of-process scan takes **~5.4 minutes**, so progress reporting and incremental persistence both matter.
2. **The watchdog is not optional — it fired on 3 plug-ins.** Two AUs (`aumf,GEMX,ksWV`, `aufx,SNXS,SSLN`) and `WaveShell1-VST3 16.7.vst3` *hung* rather than crashed. JUCE's own example would spin forever on any of them (`if (response.state == timeout) continue;`, no deadline), so an unattended scan of this machine would never finish. Waves shell plug-ins hanging matches the prior art exactly.
3. **JUCE cannot kill a hung child, for a structural reason.** `ChildProcessCoordinator::killWorkerProcess()` sends a kill *message over the pipe* and drops the connection — it sends no signal. The worker's ping thread does notice the dead connection, but reports it through `triggerAsyncUpdate()`, i.e. on the **message thread**, which is exactly the thread wedged inside the hung plug-in. Net effect: hung children outlive the app as idle orphans (observed: a child still alive at 4m29s, 0% CPU, after its coordinator had exited).
   **Fix adopted:** the child runs its own watchdog thread and calls `std::_Exit(0)` when a single probe overruns. The coordinator sends its deadline in the scan message so the child's limit tracks it, plus a 5 s margin so the coordinator still denylists first. The child is the only layer that can act here, being the only one not dependent on the wedged thread.
4. **`KnownPluginList::getBlacklistedFiles()` returns a reference to the live array**, so removing entries while iterating it silently skips half of them. Copy before mutating.
5. **Rig state round-trips** (`tests/rig_tests.cpp`, built early): a two-block rig with an embedded A2 capture serialises to ~193 KB and restores lane order, bypass flags, I/O gains, the capture (with no file access) and a NAM parameter exactly. A rig naming an uninstalled plug-in restores everything else and reports the missing one; a rig from a newer schema version is refused with a message.

## Open items to verify at build time

Items 1, 2 and 6 are resolved above. Remaining:

1. AU enumeration/instantiation from inside Logic — empirical test in P4 (the flagship DAW case). Note we can now enumerate 797 AUs standalone, so the question is purely about the sandboxed-in-DAW case.
2. Settings/known-plugins file location reachable under DAW sandboxes (GarageBand test; Unify's lesson).
3. Multi-MB state chunks in Logic autosave — test once real rigs exist.
4. Whether third-party plugins that are *licensed* pop authorisation dialogs during scan or instantiation, and how that interacts with the out-of-process scanner's watchdog. The P0 spike deliberately used only Apple AUs and our own plugin to avoid this; P2's full-corpus scan is where it surfaces.
