# BlockRig (working title) — block-based rig host with built-in NAM

**Status: v1 feature set built (P0–P11); live verification and ship work outstanding.** Pivoted
2026-07-29 from a dual-capture NAM plugin, whose DSP engine survives as this product's built-in
NAM block. The host, UI, and stage features are implemented and the test suite is green.

**The product:** a good-looking standalone app *and* VST3/AU plugin where you pick an audio
input, an output, and chain blocks in between — each block being the built-in NAM amp modeler or
any VST3/AudioUnit installed on your machine. Horizontal pedalboard lane UX, managed floating
windows for third-party editors, per-block CPU metering, portable rig files with embedded plugin
state.

## Where things stand

| Phase | State |
|---|---|
| P0 De-risk & re-base | Complete |
| P1 Chain engine + NAM block | Complete |
| P2 Out-of-process scanning + catalog | Complete (857 probes → 860 types in ~5.4 min; 3 hangers denylisted) |
| P3 Functional lane UI | Complete |
| P4 Rig state + DAW builds | Mostly complete — DAW pass folded into P7 |
| P5 CPU meter + visual design | Mostly complete; superseded by the UI overhaul (docs/18) |
| P6 Hardening | Ongoing |
| **P7 Live verification round** | **Open — needs a human at the machine** (mic/Downloads consent, TONE3000 API shapes, MIDI, Logic/Reaper pass) |
| P8 Utility blocks + error tiles | Built |
| P9 Editing quality of life | Built (undo/redo, favourites, copy/paste) |
| P10 Setlists, gig view, MIDI phase 2 | Built |
| P11 Spillover | Partly built — snapshot ducking + per-block tails ring out; rig-switch spillover not built |
| P12 Ship (signing, notarization, DMG) | Not started |

Beyond v1: [docs/19-GRAPH-ENGINE.md](docs/19-GRAPH-ENGINE.md) replaces the lane/stage model with
free-form routing on a snapping patch canvas. **The engine is built and live (G1-G2, 2026-08-04):**
rigs are a DAG of nodes and wires, latency aligns at every merge, removed blocks ring out through
whatever they fed, and rigs save as `schemaVersion` 2 with v1 files migrating on load. The patch
canvas and its gestures (G3-G5) are still to come — until then the lane UI drives the graph through
a transitional projection.

## Documents (read in order for the current product)

| Doc | Contents |
|---|---|
| [docs/10-PRODUCT.md](docs/10-PRODUCT.md) | Product brief, v1 scope, positioning, success criteria |
| [docs/11-RESEARCH.md](docs/11-RESEARCH.md) | Hosting/UX research findings + open verification items |
| [docs/12-ARCHITECTURE.md](docs/12-ARCHITECTURE.md) | Chain engine (why not AudioProcessorGraph), scanning, CPU meter, dual deployment |
| [docs/13-UI-UX.md](docs/13-UI-UX.md) | Lane design, editor-window policy, meter UX, visual direction |
| [docs/14-SCHEMA.md](docs/14-SCHEMA.md) | Normative rig state schema (`.blockrig` file == DAW chunk) |
| [docs/15-NAM-BLOCK.md](docs/15-NAM-BLOCK.md) | Built-in NAM block spec + reuse map from existing code |
| [docs/16-BUILD-PLAN.md](docs/16-BUILD-PLAN.md) | Phases P0–P12 with acceptance criteria and risks |
| [docs/17-COMPETITIVE.md](docs/17-COMPETITIVE.md) | QC CorOS 4 / Helix Stadium / GP5 / TONEX vs BlockRig |
| [docs/18-UI-OVERHAUL.md](docs/18-UI-OVERHAUL.md) | Visual overhaul spec (shipped) |
| [docs/19-GRAPH-ENGINE.md](docs/19-GRAPH-ENGINE.md) | Patch-canvas graph engine (G1–G2 built and live; G3–G5 outstanding) |
| [docs/archive-nam-plugin/](docs/archive-nam-plugin/) | First incarnation's docs (verified NAM facts, deferred capture design) |

## Build

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

Builds `BlockRigApp` (standalone) and `BlockRigPlugin` (VST3/AU; `auval` passes). Tests:
`dsp_tests`, `graph_tests`, `migration_tests`, `chain_tests` and `rig_tests` all green;
`scan_tests` is a soak test, disabled by default. `bench` covers the NAM engine (A2 ≈ 3.4% of one
core per instance at 48 kHz/128).

Diagnostics live behind CLI flags on the standalone app: `--scan`, `--audio-check`,
`--plugin-check <name>`, `--chain-check <a,b,...>` / `--chain-check session`.

## Licensing

**BlockRig is licensed under the GNU AGPLv3** — see [LICENSE](LICENSE). This follows JUCE's free
tier, which requires derived source be AGPLv3 or GPLv3; releasing under anything more permissive
would mean buying a JUCE Indie/Pro licence first.

Dependencies are all compatible: NeuralAmpModelerCore MIT (must link `WHOLE_ARCHIVE` — see
archived research), VST3 SDK MIT (Steinberg relicense, Nov 2025 — attribution required), AU via
Apple system API, Eigen MPL-2.0, melatonin modules MIT. No VST2 hosting (legally closed).
Kushview Element is GPL: reference only, never copy.
