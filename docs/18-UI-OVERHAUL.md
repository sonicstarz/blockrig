# 18 — UI overhaul (design_handoff_blockrig_ui)

Source of truth: `~/Downloads/design_handoff_blockrig_ui 2/` — README + HTML canvas
+ `screenshots/` (2× PNGs of every approved screen; the "2" bundle added these).
Verify against the screenshots by running the app, not by reading code — that
difference is what made the first S4 attempt bad.
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
- **S4 Editors** — **DONE (2026-08-02, verified against the mock screenshots in
  the running app)**: shared editor chrome in `theme::editor` (76px knob cells,
  mono value under knob then sentence-case caption; the first S4a attempt only
  tinted arcs and was redone). Panels lend controls to the BlockWindow title bar
  (`setTitleBarContent` + `setSubtitle`): NAM gets filename + Library/Open/Clear
  + EQ/Gate/Stereo toggles (4c), IR gets its file buttons (4f), EQ gets the band
  chip row placed after the title (4g). IR body: teal knobs + inset impulse well
  drawn from the loaded file (signed per-bucket peaks) with "N ms · N kHz"
  caption. EQ body: full rebuild — response graph mirroring the processor's
  coefficient math, drag handles (drag = freq/gain, wheel = Q, click selects),
  Freq/Gain/Q knobs for the selected band, Band-on (new `ls/b1/b2/hs_on` params,
  default on, gated in processBlock). Rejected all-caps captions swept out of
  NAM/IR/EQ/Utility panels and the picker.
  Fixed along the way: home-tile crash (openRig took `const File&` into code
  that destroys the File's owner — now by-value + deferred activation), and a
  systemic mojibake sweep (non-ASCII UI literals read as Latin-1 → ellipses now
  ASCII, ·/—/• wrapped in `String::fromUTF8`).
- **S5 Picker/menus/dialog** — **DONE (2026-08-02, verified in the running app)**:
  4d picker rebuilt (560px popover: category chip filter row, 52px rows with icon
  chip + "Vendor · Format" source line + favourite stars persisted to
  …/Application Support/BlockRig/starred-blocks.txt, starred section, amber
  selected row with 3px left bar, "N matches of M" footer; CPU badges omitted —
  no persisted per-plugin load data exists). 4e: block menu reordered to the mock
  (Open editor / Bypass / chain edits incl. new Replace… / red Remove / CPU stats
  footer); the Look is now the application-default LookAndFeel via AppShell, so
  untargeted popups (the block context menu) stop falling back to stock JUCE.
  4h: save-scene dialog rebuilt (Name field 16/600, "Saved in this scene" 44px
  checklist rows with raised fill + 18px amber checkboxes + category dots,
  Cancel + primary Save scene, Pin hidden via BlockWindow::setPinnable).
- **S6 Tuner + Gig**: 4i full-screen tuner (segmented Needle/Strobe, ±5¢ green
  zone, string pads E A D G B e, reference dropdown), 4j gig mode (setlist column
  maps songs→rigs, sections→scenes; scene pad grid with category dots).

## Approximations (per README's own allowance)
- Backdrop blur → layered translucent fills.
- Picker CPU badges: only for blocks that have run (live load), omitted otherwise.
- Space Grotesk 600 falls back to 700 (no static SemiBold in the 2.0 release).
- Reduced-motion: no macOS query in JUCE; boot animation skips when the last boot
  was < 5 s ago (relaunch loops) — full fidelity otherwise.
