# 13 — UI / UX Design

Paradigm (from 11-RESEARCH §6): **horizontal pedalboard lane**, not a node canvas. Input and
Output are literal blocks at the ends. Selected block edits inline below the lane (built-ins) or
in a managed floating window (third-party).

## Layout

```
┌────────────────────────────────────────────────────────────────────────────┐
│ HEADER   [logo]  [rig name ▾]  [save]              [CPU ▓▓▓░ 34%] [gear]   │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│ LANE                                                                       │
│  ┌──────┐   ┌──────┐   ┌──────┐   ┌──────┐   ┌───┐   ┌──────┐              │
│  │ IN   │──▶│ NAM  │──▶│ bx_  │──▶│Valh. │──▶│ + │──▶│ OUT  │              │
│  │▁▂▄ 1 │   │ 5150 │   │ SSL  │   │Verb  │   └───┘   │▁▂▄▂▁ │              │
│  └──────┘   └──────┘   └──────┘   └──────┘           └──────┘              │
│                                                                            │
├────────────────────────────────────────────────────────────────────────────┤
│ PANEL (selected block)                                                     │
│   NAM block: capture name • knobs (trim/EQ/gate/out) • model-size • modes  │
│   3rd-party: name • bypass • [Open Editor] • generic params (fallback)     │
│   IN/OUT:    device channel picker • gain trim • meter   (standalone)      │
└────────────────────────────────────────────────────────────────────────────┘
```

Default window ~1100×640, resizable; the lane scrolls horizontally when full. The panel is the
lane's complement: lane = *what's in the rig*, panel = *the thing you're touching now*.

## The lane

- **Blocks** are rounded tiles: icon + short name + a thin activity/level bar. States: normal, selected (accent outline), bypassed (dimmed, struck LED), error/missing (amber tile with reason tooltip), drag-hover.
- **Add**: click the `+` tile (always present before OUT) or double-click empty lane space → picker popup with search field and categories: *Built-in* (NAM), then Effect categories from plugin metadata (Dynamics, EQ, Reverb…), then *All Plugins* (A–Z, VST3/AU badges). Recently-used pinned at top. Type-to-search is the primary path — 125 plugins on this machine make browsing menus a chore.
- **Reorder**: drag tiles; drop indicator between tiles. **Right-click**: Bypass, Replace… (opens picker, preserves position), Remove, Open/Hide Editor, Rename, per-block latency + CPU readout.
- **Bypass** also via click on the block's LED dot. Keyboard: Delete removes selected, ⌘B bypasses, arrows move selection.
- **IN/OUT end blocks**: live meters always; clicking selects them → panel shows (standalone) device channel picker + gain + phantom "mono/stereo" mode, or (DAW) bus labels + gain. Device/driver/buffer/sample-rate live in the settings sheet (gear), not the lane — but *channel choice* lives here (Helix/GENOME precedent; "Input 1 mono" must be one click).
- Splits (v1.1): drag a block *below* the lane → auto Split/Merge appear, Helix-style. The lane reserves vertical room; don't design v1 into a corner.

## Third-party editor windows (PatchWork mitigation set, wholesale)

- One floating `DocumentWindow` per open editor, native title bar, plugin name as title.
- Position/size **remembered per block in the rig file**; restored on open. "Reopen editors on rig load" preference (off by default).
- ESC closes frontmost editor; ⌘⌥W closes all; per-window always-on-top toggle; opening a block's editor from the lane raises it if already open.
- Plugins without editors (or when the editor fails) → generic parameter list in the panel instead (JUCE's fallback pattern).

## CPU meter

- **Header**: compact horizontal bar + numeric %, EMA-smoothed, peak-hold tick decaying after ~1.5 s. Amber ≥70%, red at overload with a dropout counter badge that persists until clicked.
- **Click** → CPU panel: table of blocks with avg %, peak %, latency (samples/ms), sorted by peak; total row; overload log. One-line footnote: "Percent of the audio buffer's time budget — not Activity Monitor CPU."
- Per-block CPU is a headline feature (nothing in the guitar space ships it); surface a mini readout in each block's right-click menu too.

## Visual design

- **Idiom**: flat, dark (#101216-ish background layers), high contrast type, one accent (candidate: warm amber — differentiates from Neural DSP teal/blue crowd), subtle depth via melatonin_blur shadows, 6–8 px radii, generous spacing. No skeuomorphism, no photoreal amps; the NAM panel earns richness through big clean knobs, not textures.
- Typography: one variable sans (e.g. Inter or the JUCE-bundled default styled hard); tabular numerals for meters.
- All colors/metrics in a single `Theme` struct (`ui/theme/`) — no literals in components. Dev-time: melatonin_inspector behind a debug flag.
- Restyle `PluginListComponent` (scan/manage UI) to the theme; it lives in the settings sheet under "Plugins", with Rescan + denylist management.

## Settings sheet (gear)

Audio (standalone only: device, buffer, sample rate), Plugins (scan paths, rescan, denylist),
Appearance (accent, meter style), About (licenses: VST3 SDK MIT attribution, JUCE, NAM MIT).

## First-run

1. Standalone opens with an empty rig: IN → + → OUT, and a one-time toast: "Scan your plugins to add them as blocks" → opens Plugins settings → scan with progress (name-by-name, cancel, survives crashes visibly: "skipped 2 plugins that failed — view").
2. After scan, picker is populated; drop a NAM block; if no `.nam` files are known, the NAM panel offers "Get captures at tone3000.com" + file browser.

## Explicit non-goals in UI v1

Node canvas view; inline-embedded third-party editors (Element-style) — stretch goal; theming
beyond dark; MIDI-learn UI (schema reserves parameter-mapping space, UI later).
