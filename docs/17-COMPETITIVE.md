# 17 — Competitive analysis (2026-07-30)

Where BlockRig sits against the field, and what the field says we're missing.
Compiled from current releases: Quad Cortex CorOS 4.0.x + Quad Cortex mini (NAMM 2026),
Helix Stadium / Stadium XL, Gig Performer 5, TONEX 1.11 + ToneNET preset sharing,
plus stable knowledge of Fractal (FM3/FM9/Axe-Fx III), Kemper, Headrush, MainStage,
Camelot Pro, Blue Cat PatchWork.

## The two families we straddle

**Hardware modelers** (Quad Cortex, Helix Stadium, Fractal, Kemper, Headrush):
closed ecosystems, own DSP, hands-free/live-first UX. Their moat is hardware +
captive effects. Their weakness is exactly our strength: they can't run Valhalla,
Kiive, Waves, or any plugin the user already owns.

**Software plugin hosts** (Gig Performer, MainStage, Camelot, PatchWork): open
ecosystems like us, but keyboard-player DNA — none of them ships an amp block,
none is guitarist-first, and their UIs are racks and widgets, not pedalboards.

BlockRig's position: *the only guitarist-first host where the amp is NAM and every
other block is whatever you own.* Nobody occupies this square. TONEX is closest in
spirit (capture player + chain + sharing) but is a closed IK ecosystem.

## What we already match or beat

| Capability | Us | Notes |
|---|---|---|
| Amp modeling quality | ✔ | NAM A2 captures; the same tech QC's Neural Capture competes with. Capture *library* with folders ✔ |
| Scenes/snapshots | ✔ | Ours can swap the NAM capture per snapshot — QC scenes and Helix snapshots cannot swap models/blocks. Configurable safes per snapshot is genuinely novel |
| Third-party effects | ✔✔ | The whole point. Nobody in the hardware family has this |
| Tuner | ✔ | Needle + strobe + reference. Comparable to QC/Helix |
| Tap/BPM/transport to all blocks | ✔ | |
| Parallel/dual-amp paths | ✔ | A/B split, dual-mono or parallel. QC has 2×2 grid rows, Stadium has larger routing — see gaps |
| Rigs as portable files | ✔ | Our sharing story: a .blockrig embeds every plugin's state incl. captures |
| DAW use | ✔ (unverified) | VST3/AU build exists; P4 in-DAW pass still pending |
| CPU/latency honesty | ✔ | Per-block load, dropout counter |
| Boot scan / device recall / dirty-save flow | ✔ | Now comparable to hardware boot UX |

## Gaps, ranked by how loudly the market says they matter

### 1. MIDI control — table stakes, we have none
Every single competitor: program change → preset/rig, CC → any parameter, expression
pedal input, MIDI clock sync for tempo. Gig Performer adds bidirectional control.
Without this, BlockRig cannot be played live hands-free, which kills the
pedalboard-replacement story. **Build: MIDI in (PC → rig/snapshot, CC learn → any
hosted parameter via AudioProcessorParameter, expression mapping, MIDI clock in/out).**
This is the single highest-leverage missing feature.

### 2. Spillover / seamless switching — the 2026 battleground
Helix Stadium's headline: delay/reverb tails ring across snapshot changes; Gig
Performer's Patch Persist does it across rackspaces. QC still audibly gaps between
presets. Our snapshot apply (setState in place) partially preserves tails but any
plugin that resets buffers on setState will click. Rig switching is a full teardown —
guaranteed gap. **Build later: tail-carry — keep the outgoing chain rendering into a
mix bus for N seconds after a switch. Hard (double CPU during overlap) but it's the
feature reviews measure now.**

### 3. Per-block favorites, copy/paste, undo — editing quality of life
Stadium: block favorites (a dialed-in block one tap away in any preset), copy/paste
blocks, undo/redo, preset templates, a librarian for all assets. Fractal's *global
blocks* (edit once, updates every preset using it) is the power-user version.
We have none of this. **Build: block favorites = named plugin-state chunks (reuse the
capture-library pattern); copy/paste block incl. state; undo as serialized-rig ring
buffer (we already serialize cheaply for dirty checks).**

### 4. Setlists + gig view
QC: Gig View (CorOS 4 made it footswitch-accessible); Helix: setlists as first-class
objects; Gig Performer/Camelot: setlists with song parts driving sound changes.
We have a rigs folder sorted by mtime. **Build: ordered setlists over rigs, and a
performance view — huge rig/snapshot buttons readable on a dark stage.**

### 5. Built-in utility blocks
Hardware ships loaded: IR loader, EQ, gain/pan, phase, mixers. We defer to
third-party plugins, but a rig shouldn't need a paid plugin for a gain trim.
Already on the deferred list. **Build: IR loader (cab after NAM is the #1 pairing),
gain/pan/phase utility, simple graphic EQ. Cheap, high perceived completeness.**

### 6. Community sharing
ToneNET now does full-rig preset sharing browsable by artist/genre; QC has cloud
capture sharing baked in. Our answer is files + the NAM ecosystem's own Tone3000.
**Do not build a cloud. Do: one-click "export rig" (already self-contained) and a
docs page pointing at Tone3000 for captures. Revisit if the user base exists.**

### 7. Looper + metronome
Headrush/Ampero/Boss ship loopers; QC took years to add one. Metronome is nearly
free with our transport. **Build metronome soon; looper is a later, contained block.**

### 8. Deeper routing
QC: two full 2-row grids per preset (4 lanes); Stadium: bigger matrix + send/return
for hardware pedals. We have one A/B split per stage. Multiple splits work but
nesting doesn't. Hardware send/return is N/A for software; audio-interface
multi-out routing (>2 outs, e.g. wet/dry/wet) is the software equivalent.
**Later: per-stage output taps → assignable device channels.**

### 9. Capture creation
QC captures on-device; TONEX/Kemper likewise. Our archived design (M-series NAM
trainer) is the eventual answer; NAM training needs a GPU/time budget hardware can't
match locally. Out of scope for now, on the record as roadmap.

### Non-goals (explicitly)
- Own effects DSP (we host; that's the identity)
- Cloud backend (files are the exchange format)
- Predictive loading (GP's RAM saver — we keep one rig loaded; revisit only if
  giant rigs appear)
- Hardware I/O features (impedance, FX loop jacks)

## Suggested order of attack

1. **MIDI** (PC/CC/expression/clock) — unlocks live use, touches everything after it
2. **Utility blocks: IR loader + gain/phase/EQ + metronome** — completeness, cheap
3. **Block favorites + copy/paste + undo** — daily-driver comfort
4. **Setlists + gig view** — the stage story, builds on MIDI
5. **Spillover** — the flagship differentiator, hardest, save for when 1–4 are solid
