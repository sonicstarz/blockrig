# 18 — UI overhaul (design_handoff_blockrig_ui)

Source of truth: `~/Downloads/design_handoff_blockrig_ui/` — README + HTML canvas.
Approved: Turn 4 (`4a`–`4j`) with `5a` replacing `4b`. High fidelity. One global
correction from review: **sentence-case labels in the UI face**; mono reserved for
numerics (BPM, dB, Hz, ms, CPU %).

## Stages

- **S1 Foundation**: Space Grotesk + IBM Plex Mono embedded (OFL, in `assets/fonts`,
  via juce_add_binary_data). Theme tokens rewritten to the violet-black system
  (ground #0B0A10 / raised #15131D / raised-2 #1D1A28 / inset #100E16 / border
  #2C2839 / hairline #221F2E; text F4F5F7 / D6D3E0 / 9B96AD / 635F75; amber trio
  E8A33D–F2C069–C98A2E; success 5ED669; danger FF6B70). Old token names kept as
  aliases so every component re-skins at once. LookAndFeel: primary-gradient
  buttons (properties flag), rounded-square amber checkboxes, overlay popup menus,
  soft-glass rotary knob (270° track #221F2E 7px, category-coloured value arc with
  glow, radial-gradient centre disc, rotating needle, mono value box). Category
  palette to spec (Drive E5484D · Amp E8A33D · Cab 2EBFA5 · EQ 4C8DFF · Mod 5BC24C
  · Delay 35B6E0 · Reverb 9B6DF2 · Pitch E5559C · Utility 7B8494). Logo: 2×2
  rounded squares, three #3A3644 outlines + solid amber bottom-right; wordmark
  BLOCK(#F4F5F7)+RIG(amber).
- **S2 Boot + Home**: 4a fly-in (staggered corners, glow landing, progress bar with
  "scanned/total" — scan progress gains counts), 5a tvOS shelves (Rigs 300×176
  focus-scaled tiles with category dots, Setlists, Tools; dashed "+" tiles).
- **S3 Rig page**: 4c header (preset pill with ‹›, amber unsaved dot, BPM cluster,
  mini meters, Tuner secondary + Gig primary, ⋯ overflow), scene row (letter+name
  pads, amber active), chain (64px chips, amber wires fading at endcaps, IN/OUT
  endcaps with signal bars, byp at 45%, active dot badge), **docked editor** at the
  bottom replacing floating block windows (Pin stacks; utility panels stay as
  dialogs/menus).
- **S4 Editors**: knob spec everywhere; NAM (amber), IR (teal + impulse waveform
  well), EQ (blue + interactive response graph with band handles + band chips).
- **S5 Picker/menus/dialog**: 4d category-chip picker with favourite stars, 4e menu
  styling (comes mostly free from S1 LnF), 4h save-scene dialog.
- **S6 Tuner + Gig**: 4i full-screen tuner (segmented Needle/Strobe, ±5¢ green
  zone, string pads E A D G B e, reference dropdown), 4j gig mode (setlist column
  maps songs→rigs, sections→scenes; scene pad grid with category dots).

## Approximations (per README's own allowance)
- Backdrop blur → layered translucent fills.
- Picker CPU badges: only for blocks that have run (live load), omitted otherwise.
- Space Grotesk 600 falls back to 700 (no static SemiBold in the 2.0 release).
- Reduced-motion: no macOS query in JUCE; boot animation skips when the last boot
  was < 5 s ago (relaunch loops) — full fidelity otherwise.
