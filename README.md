# BlockRig (working title) — block-based rig host with built-in NAM

**Status: pivoted 2026-07-29 — design complete, host implementation not started.** The previous
incarnation (a dual-capture NAM plugin) was built and verified through its M4 milestone; its DSP
engine survives as this product's built-in NAM block, and its plugin builds still compile and
pass tests.

**The product:** a good-looking standalone app *and* VST3/AU plugin where you pick an audio
input, an output, and chain blocks in between — each block being the built-in NAM amp modeler or
any VST3/AudioUnit installed on your machine. Horizontal pedalboard lane UX, managed floating
windows for third-party editors, per-block CPU metering, portable rig files with embedded plugin
state.

## Documents (read in order for the current product)

| Doc | Contents |
|---|---|
| [docs/10-PRODUCT.md](docs/10-PRODUCT.md) | Product brief, v1 scope, positioning, success criteria |
| [docs/11-RESEARCH.md](docs/11-RESEARCH.md) | Hosting/UX research findings + open verification items |
| [docs/12-ARCHITECTURE.md](docs/12-ARCHITECTURE.md) | Chain engine (why not AudioProcessorGraph), scanning, CPU meter, dual deployment |
| [docs/13-UI-UX.md](docs/13-UI-UX.md) | Lane design, editor-window policy, meter UX, visual direction |
| [docs/14-SCHEMA.md](docs/14-SCHEMA.md) | Normative rig state schema (`.blockrig` file == DAW chunk) |
| [docs/15-NAM-BLOCK.md](docs/15-NAM-BLOCK.md) | Built-in NAM block spec + reuse map from existing code |
| [docs/16-BUILD-PLAN.md](docs/16-BUILD-PLAN.md) | Phases P0–P6 with acceptance criteria and risks |
| [docs/archive-nam-plugin/](docs/archive-nam-plugin/) | First incarnation's docs (verified NAM facts, deferred capture design) |

## Current build (previous incarnation — still green)

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

Builds the dual-slot NAM plugin (VST3/AU/Standalone; `auval` passes). `src/dsp/` — the part that
matters going forward — is covered by `dsp_tests` and benchmarked by `bench` (A2 ≈ 3.4% of one
core per instance at 48 kHz/128).

## Licensing

All permissive: NeuralAmpModelerCore MIT (must link `WHOLE_ARCHIVE` — see archived research),
VST3 SDK MIT (Steinberg relicense, Nov 2025 — attribution required), AU via Apple system API,
Eigen MPL-2.0, JUCE Starter/AGPL/commercial (choose at release), melatonin modules MIT. No VST2
hosting (legally closed). Kushview Element is GPL: reference only, never copy.
