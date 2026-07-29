# 10 — Product Brief: BlockRig (working title)

**One sentence:** a good-looking block-based rig host — pick your input, pick your output, and
chain blocks in between, where a block is either the built-in NAM amp modeler or any VST3/AU
plugin on your machine — running both as a standalone app and as a VST3/AU inside a DAW.

> Working title "BlockRig" is a placeholder; rename freely. The repo keeps its NAM-Modeler name
> for now.

## Who it's for

Guitarists building amp rigs on a Mac (Windows later). The mental model is a pedal chain, not a
modular patchbay.

## Core feature set (v1)

1. **The lane.** A horizontal chain of blocks. The Input block sits at the far left, the Output block at the far right — they are real, clickable blocks (Helix-style), not settings buried in a dialog. Blocks in between are added from a categorized picker, reordered by drag, bypassed/replaced/removed in place.
2. **Blocks are plugins.** Any scanned VST3 or AudioUnit on the machine, plus built-in blocks. Third-party editors open as managed floating windows; built-in blocks edit inline in a large panel under the lane.
3. **Built-in NAM block.** The amp engine we already built and verified: loads any `.nam` capture (A1, LSTM, A2 incl. slimmable), input/output trim, 3-band tone stack, noise gate, output modes, dBu calibration, A2 model-size — with the `.nam` embedded in state so rigs are portable.
4. **CPU meter.** Header meter showing % of the audio-callback budget with peak-hold and overload indication; click through to a per-block breakdown (avg %, peak %, latency). No guitar host ships per-block CPU today — this is a differentiator.
5. **Dual deployment.** One codebase, one `juce_add_plugin` target: Standalone (own audio device selection, the primary experience) and VST3/AU (the same lane inside a DAW; Input/Output blocks then represent the host's buses).
6. **Looks good.** Flat, dark, high-contrast, restrained accent color — the Neural DSP / GENOME idiom — via a custom LookAndFeel and melatonin_blur shadows. Not JUCE-default gray.

## Deliberately out of v1 (schema reserves room; see 14-SCHEMA.md)

- Parallel split/merge rows (v1.1 — the schema models the lane as stages so splits slot in without migration)
- Setlists, snapshots, A/B compare (v1.x)
- Capture creation (designed in `archive-nam-plugin/04-CAPTURE.md`, still deferred)
- VST2 (legally closed to new hosts), AAX, LV2, Windows build, oversampling, MIDI routing

## Positioning

Closest existing products: Blue Cat PatchWork (reliable but grid-constrained and dated-looking),
Two Notes GENOME (lovely lane but a closed ecosystem — only its own block types plus capture
loaders), Kushview Element (free canvas, utilitarian). Nothing combines: guitarist lane UX +
*any plugin as a block* + first-class NAM + per-block CPU visibility + looks good. That
combination is the product.

## Success criteria for v1

- Guitar → interface → standalone app: pick input, drop NAM block, drop a third-party reverb VST3, hear a rig, at zero added latency beyond the blocks themselves.
- The same rig file opens inside Logic (AU) and Reaper (VST3) with every block restored, including third-party plugin state.
- A hostile plugin crashing during scan does not take the app down, and gets denylisted.
- The CPU panel correctly identifies which block is eating the budget.
- Someone screenshots it and it doesn't look like a JUCE demo.
