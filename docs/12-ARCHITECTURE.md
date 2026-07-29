# 12 — Architecture

## Targets

One `juce_add_plugin` target, `FORMATS Standalone VST3 AU`, C++20, CMake, macOS first
(arm64; universal later). The Standalone uses a **custom shell**
(`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`) so we own the `AudioDeviceManager`, menus, and
window — required because I/O device state is part of the product (the Input/Output end blocks),
not a generic options dialog. Hosting flags on the shared target: `JUCE_PLUGINHOST_VST3=1`,
`JUCE_PLUGINHOST_AU=1` (no VST2 — legally closed).

**JUCE version:** bump from the vendored 8.0.9 before host work starts — 8.0.9 has a VST3
host-side loading regression. Pick the newest 8.0.x whose forum status is clean, then re-run the
existing NAM tests (`ctest`) to confirm the bump breaks nothing.

Runtime branch between deployments: `wrapperType == wrapperType_Standalone`. In a DAW, the
Input/Output blocks represent the host's buses (fixed stereo in/out, labeled "From DAW" / "To
DAW"); standalone, they map device channels.

## Repo layout (target state)

```
src/
├── app/                      # custom standalone shell (device mgr, main window, settings)
├── host/
│   ├── BlockChain.{h,cpp}        # the engine: ordered blocks, snapshot swap, PDC, bypass
│   ├── BlockInstance.{h,cpp}     # one block: AudioPluginInstance + timing + bypass + state
│   ├── PluginCatalog.{h,cpp}     # KnownPluginList wrapper, scan orchestration, denylist
│   ├── ScannerSubprocess.{h,cpp} # ChildProcessWorker entry (runs in relaunched executable)
│   ├── InternalBlockFormat.{h,cpp} # registers built-in blocks (NAM) as an AudioPluginFormat
│   └── CpuMeter.{h,cpp}          # per-block + total callback-budget timing
├── blocks/
│   └── nam/                      # NAM block processor + inline editor (wraps existing src/dsp)
├── dsp/                      # UNCHANGED: AmpSlot, ResamplingNam, ToneStack, NoiseGate, ModelLoader
├── state/                    # rig serialization per 14-SCHEMA.md (+ existing NAM state code)
├── ui/
│   ├── lane/                     # the pedalboard strip: LaneView, BlockView, IO end blocks, picker
│   ├── panel/                    # inline editor panel for built-in blocks
│   ├── windows/                  # floating-window manager for third-party editors
│   └── theme/                    # LookAndFeel, palette, melatonin_blur usage
├── PluginProcessor.{h,cpp}   # REWRITTEN: owns BlockChain, rig state, latency reporting
└── PluginEditor.{h,cpp}      # REWRITTEN: lane + panel + header (CPU meter, preset bar)
```

`src/dsp/` and its tests survive verbatim — that engine becomes the NAM block's guts. The old
`PluginProcessor`/`PluginEditor` (dual-slot product) are replaced; salvage the parameter
attachment patterns and the state gzip helpers.

## The chain engine (why not AudioProcessorGraph — see 11-RESEARCH §2)

**Model:** a rig is an ordered list of *stages*; each stage holds one block (v1) or 2 parallel
rows of blocks (v1.1 — schema-ready now, engine later). A block is a `BlockInstance` owning an
`AudioPluginInstance` — third-party VST3/AU or a built-in served by `InternalBlockFormat`. The
whole lane runs stereo float; mono-only plugins get JUCE's layout negotiation with
mono-sum-in/duplicate-out fallback.

**Real-time discipline** (the same pattern we shipped and verified in `AmpSlot`, promoted to
chain scope):

1. All edits (add/remove/reorder/replace) happen on the message thread by building a **new chain snapshot** (array of block pointers + per-block PDC delays + total latency).
2. Snapshot published via atomic pointer; audio thread adopts at block top; the retired snapshot goes to a retirement queue freed on the message thread. Audio thread never allocates, locks, or frees. (This mirrors what APG does internally, minus its per-node `CriticalSection` per block and its silence-gap on prepare changes.)
3. Blocks being *removed* keep processing until the swap lands, then are released off-thread. Blocks being *added* are instantiated + `prepareToPlay`'d + (for NAM) prewarmed before entering a snapshot.
4. A short crossfade (~20 ms) between old and new snapshot output masks edits made while audio runs. v1 fallback: fade-through-silence.
5. Bypass: prefer the hosted plugin's own `getBypassParameter()`; else `processBlockBypassed()`. A bypassed latent block still reports its latency (JUCE's default bypass does not delay-compensate — known limitation, documented).

**Latency / PDC:** total = Σ active blocks' `getLatencySamples()`, reported via our
`setLatencySamples` (DAWs may apply it only on transport stop — documented, PatchWork precedent).
Poll blocks' latency each snapshot rebuild *and* on a low-rate timer (~1 s) to catch mid-flight
changes (lookahead toggles etc.); a change triggers a snapshot rebuild. Parallel rows (v1.1):
delay-line pad on the shorter row, exactly what APG's `DelayChannelOp` does.

**Buffers:** hosts can exceed the promised block size — chunk defensively (existing pattern).
Standalone owns its device via `AudioDeviceManager` + `AudioProcessorPlayer`.

## Plugin scanning & catalog

Copy the AudioPluginHost reference wholesale, plus the missing watchdog:

- Relaunch our own executable with a scanner UID (`ChildProcessWorker` in `ScannerSubprocess`); coordinator side implements `KnownPluginList::CustomScanner`; **out-of-process is the only mode we ship** (in-process scanning is a debug flag).
- Dead-man's-pedal file + `applyBlacklistingsFromDeadMansPedal` + denylist UI (`PluginListComponent` handles display; we restyle it).
- **Watchdog, in two halves — both are required** (60 s per plugin by default):
  - *Coordinator side*: give each probe a deadline; on expiry drop the child, denylist the plugin, move on. This is what unblocks the scan (JUCE's example has no deadline and spins forever).
  - *Child side*: the child runs its own watchdog thread and `std::_Exit(0)`s if a probe overruns its own (slightly longer) limit, which the coordinator passes in the scan message. **This half cannot be skipped**: `killWorkerProcess()` only sends a message over the pipe, and JUCE's connection-lost notification is delivered via `triggerAsyncUpdate()` on the message thread — the very thread wedged inside the hung plugin. Without it, hung children survive as orphans (measured; see 11-RESEARCH §P2).
  - Verified against this machine: 3 plugins hang rather than crash (two Waves/SSL AUs and the Waves VST3 shell).
- Persist the plugin list after every few successful probes: a later crash then costs one plugin, not the whole 5-minute run.
- Catalog persists as XML in an app-support path chosen to be readable under DAW sandboxes (Unify's GarageBand lesson — **verify location empirically in Logic/GarageBand**). Scans run from the standalone; the plugin build reads the catalog and can trigger a rescan but warns it's better done standalone.
- AUv3: async instantiation, message thread, best-effort inside DAWs.

## Built-in blocks

`InternalBlockFormat` (the AudioPluginHost `InternalPluginFormat` pattern) serves built-ins as
`PluginDescription`s with a reserved format name (`"BlockRig"`), so the picker, chain, and
schema treat them uniformly with third-party plugins. v1 ships one: **NAM** (15-NAM-BLOCK.md).
The Input/Output end blocks are *not* chain blocks — they're the chain's I/O binding plus UI.

## CPU meter

- Wrap every block's `processBlock` call with steady-clock timestamps inside `BlockInstance` — we own the call site, so attribution is exact. Publish per-block EMA + CAS-max peak as relaxed atomics; a UI timer reads at ~15 Hz.
- Total = whole-callback time ÷ block duration (standalone can cross-check against `AudioDeviceManager::getCpuUsage()`). Overload = callback time > budget; count and surface dropouts.
- Presentation per 13-UI-UX: header meter (avg + peak-hold tick, amber ≥70%, red = overload), click-through panel listing each block's avg %, peak %, and latency samples. In-app copy states it measures the audio callback budget, not Activity-Monitor CPU.

## State

Full schema in 14-SCHEMA.md. Principles: one `ValueTree` rig document; third-party block state
embedded base64 (their own `getStateInformation` blobs); built-in NAM keeps its embedded-gzipped-
`.nam` approach; big external payloads by reference; schema versioned from day one; editor window
positions per block persisted (PatchWork behavior). Same document = standalone rig file
(`.blockrig`) = DAW chunk.

## Error containment

- Instantiation failure → block renders as an error tile in the lane holding its description + saved state, so a missing plugin doesn't destroy the rig (reinstall → works again).
- A hosted plugin throwing in `processBlock`: JUCE doesn't sandbox audio processing in-process; we do **not** try/catch around audio (UB anyway). Containment = the scan denylist + (v2 option) AudioGridder-style out-of-process hosting if ever justified.
- Missing `.nam`/sidecar files → the NAM block's existing embedded-state behavior already covers it.

## Performance sanity gates

Benchmark early (11-RESEARCH §7 flags an unconfirmed 7× JUCE-wrapper-hosting overhead report):
host `bx_rockrack V3 Player` standalone-in-BlockRig vs directly in Reaper, compare CPU. If
overhead is real and large, investigate before building more. Existing `bench` tool gives NAM
block numbers (A2 ≈ 3.4%/core at 128).
