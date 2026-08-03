# 19 — Graph engine & patch canvas (plan)

**Status: PLANNED 2026-08-02, not started.** Supersedes the stage/row chain model
(docs/12 §chain engine) and the lane UI (docs/13) once complete. Written before
any code, per the pivot request: free-form routing — drag blocks on a snapping
grid, wire them visually, split and merge signal paths by gesture instead of the
per-stage A/B menu.

## 0. The idea in one paragraph

The rig stops being a line with optional A/B stages and becomes a **directed
graph on a snapping grid**. A block is a node; a wire connects one block's
output to another's input. Splitting is not a mode and not a special block:
**drag three wires out of one output and you have three signal paths.** Merging
is the mirror: wires into the same input sum. The canvas reads left → right like
the current lane, but any block can feed any block ahead of it. Everything else
BlockRig believes — scenes recall sound while rigs own structure, the audio
thread never allocates, removed blocks ring out — stays true.

## 1. Model

### Nodes
- A node is a `BlockInstance` (unchanged: uid, plugin or built-in, bypass,
  state) plus a **grid position** `(col, row)`. IN and OUT become real nodes on
  the canvas — they already paint as endcaps; now they participate in wiring.
- One audio input port, one audio output port per node (v1). Ports are the
  whole left/right edge of the chip — no fiddly pin targets at stage distance.

### Wires
- `wire = { fromUid, toUid }`, always output → input, always column-increasing
  (see Grid). Unity gain, no per-wire processing.
- **Fan-out is free**: one output may feed any number of inputs. This is the
  splitter. No splitter block needed for signal fan-out.
- **Fan-in sums**: any input may receive any number of wires; they are summed
  before the block runs. For *weighted* merges (blend 70/30, mute one branch),
  drop in the **Mixer** utility block (new built-in: N inputs via fan-in, per-
  source trim… v1: a simple Gain on each incoming branch is achieved by putting
  a Utility block on that branch, so Mixer itself is deferred — fan-in sum +
  Utility-per-branch covers it with blocks we already have).
- Distinct from fan-out (same signal, N places), *content* splits stay blocks:
  the existing dual-mono split generalizes to a **Split L/R** utility node
  (1 in, 2 out) later; a crossover (low/high split) is another. Multi-output
  utility nodes are schema-ready in v1 (ports are arrays) but only 1-in/1-out
  ships first.

### Rules
- **DAG only.** A wire that would create a cycle is rejected at the gesture
  (the wire snaps back, the target flashes). Feedback loops are a possible
  v2 with an explicit one-block delay node; not v1.
- Nodes that do not reach OUT are legal but **dormant**: not rendered, drawn
  dimmed at 45% like bypass, labelled "not connected". A parked block is a
  feature (stash a lead sound off to the side), not an error.
- Width (mono/stereo) propagates along wires: a node negotiates mono-in when
  *all* its incoming branches are mono; `WidthNeutral` pass-through and the
  merge rule (merge width = widest input) generalize the current per-stage
  negotiation. Renegotiation still runs under `suspendAudio`.
- **Latency alignment**: parallel branches can carry different plugin latency.
  The compiler computes per-path latency and inserts alignment delays at each
  fan-in so branches stay phase-coherent — this is new (the A/B model mostly
  dodged it) and is the main DSP correctness risk. Total graph latency
  publishes to the host exactly as `mPublishedLatency` does today.

## 2. Engine

The proven machinery is kept whole; only the compiled shape changes.

- Message thread compiles `Graph → RenderPlan`: topological order, a reused
  buffer pool (colouring by liveness, not one buffer per node), per-input sum
  lists, alignment delays, width map. No allocation, locks, or logging on the
  audio thread; the plan is immutable once published.
- Publish via the existing atomic-pointer swap; old plans retire through the
  existing retirement queue.
- **Spillover generalizes**: a removed node keeps rendering silence-fed into
  the input(s) it used to feed until its tail dies — same as today, but the
  retirement record carries its former destinations instead of its former
  stage. (This also fixes the known limitation noted in BlockChain.h: retired
  blocks currently render alone rather than with topology intact.)
- Per-block CPU/latency metering is per-node already — unchanged.
- Parallel branch rendering on multiple threads is explicitly **not** v1; the
  topo order runs serially like today's stages.

## 3. Schema (docs/14 bump — breaking shape change)

- `lane { stages { rows { blocks } } }` → `graph { nodes[], wires[] }`; node
  gains `col`, `row`; wires are uid pairs. `schemaVersion` bumps.
- **Migration is deterministic and fixture-tested** (per docs/14 policy):
  - linear chain → one row of nodes, consecutive wires
  - split stage → fan-out from the previous node into row 0 / row 1 chains,
    fan-in at the next node
  - fixtures: empty rig, linear, one split stage, split with uneven branch
    lengths, split at chain start/end, the current user rigs in test corpus.
- Scenes (uid → state) and MIDI mappings (uid-keyed) survive untouched.
- Old app versions cannot open new rigs; migration is one-way with a `.bak`
  of the pre-migration file written beside the rig.

## 4. Canvas UX

The lane view is replaced by a **patch canvas**. Same visual language (72px
category chips, amber wires, names beneath), new interaction model.

### Grid
- Snap grid, columns flow left → right, rows stack parallel paths. No free
  pixel placement and no zoom in v1: the grid *is* the readability guarantee.
- IN pinned in the leftmost column, OUT in the rightmost; both movable in row.

### Gestures (the heart of the request)
| Gesture | Result |
|---|---|
| Drag from a chip's right edge | Wire follows the cursor; drop on a chip (or its left edge) to connect. Invalid targets (cycle, self) refuse visibly. |
| Drop a picked/dragged block **onto a wire** | Splice: the wire splits into `src → new → dst`. |
| Drop a block **into the empty cell below/above a block** | Branch: the new node copies that neighbour's wiring — same source(s), same destination(s) — so "move a block under the NAM" instantly means "a parallel path around/beside it". This is the "it wires itself in" behaviour. |
| Drop a block into empty space elsewhere | Dormant node, dimmed, no wires. Wire it when ready. |
| Drag a block out of a chain | The wire **heals** (source reconnects to destination), same as removal today. |
| Click a wire | Select; Delete removes; drag either end to re-patch. |
| Drag a wired block to a new cell | Wires follow; crossing wires auto-route. |
- Every gesture is one undo step (existing UndoHistory).
- A **Tidy** button compacts columns/rows (simple layered layout, no fancy
  algorithm — left-to-right ranks are already implied by the DAG).

### Reading the flow
- Wires draw as the existing amber connectors, rounded orthogonal routes,
  arrowheads only where direction is ambiguous (fan-in from behind).
- Fan-out draws one stub splitting into N — the visual answer to "where does
  my signal go" without tracing.
- Dormant subgraphs at 45% with "not connected"; bypass unchanged.
- Block menu: "Split this stage into A/B" and "Merge back" **disappear**
  (gestures replace them); Open/Bypass/Replace/Copy/Save-favourite/Remove stay.

## 5. What must not regress
- Audio-thread rules (CLAUDE.md) — the compiler runs on the message thread.
- Scenes, setlists, gig view, MIDI learn — all uid-based, untouched.
- Spillover behaviour (P11) — now *better* (topology-aware tails).
- `dsp_tests` / `chain_tests` stay green until the cutover phase, then
  `chain_tests` is superseded by `graph_tests` covering the same guarantees.

## 6. Phases

- **G1 — Graph core.** Graph model + compiler (topo, buffer pool, sums, width,
  latency alignment) behind the existing BlockChain interface, `graph_tests`
  for: topo order, cycle rejection, fan-out/fan-in audio equality vs hand-built
  reference, width propagation, latency alignment (two branches, unequal PDC,
  null test when re-summed), spillover-with-topology. **No UI change.**
- **G2 — Schema + migration.** New shapes, bump, migration fixtures above,
  `.bak` writing. Old lane UI renders the migrated graph's row-0 projection so
  the app stays usable mid-transition.
- **G3 — Canvas, read-only.** Draw the graph (chips, wires, dormant dimming);
  select/open/menu works; no edit gestures yet. Verified against real rigs.
- **G4 — Gestures.** Wire-drag, splice, branch-drop, heal, re-patch, delete,
  undo for each. Picker drops onto canvas cells and wires.
- **G5 — Cutover + polish.** Remove LaneView and split/merge menu items, Tidy,
  auto-route polish, docs 12/13/14/16 updated, `chain_tests` → `graph_tests`.
- Each phase: build green, ctest green, and **verified by driving the app**
  before it is called done.

## 7. Risks & open questions

- **Latency alignment correctness** is the deep risk — G1's null test is the
  gate. Until it passes, parallel paths with latency-reporting plugins would
  comb-filter exactly like the competition's cheaper hosts do.
- **Sum headroom**: implicit fan-in can clip where the old A/B averaged.
  Decision: sum at unity (industry norm for parallel paths), rely on meters +
  the Utility trim; revisit only if real rigs clip.
- **Migration fidelity** on the user's actual rigs — test against NEW NEW and
  Test 1.2.3 before cutover.
- **UI complexity creep**: the grid + seven gestures above are the whole v1.
  No zoom, no free placement, no multi-out utility nodes, no feedback, no
  multi-device outs — all listed as explicit non-goals so they don't leak in.
- Open: should dropping from the picker onto plain empty canvas auto-wire to
  the nearest upstream block, or stay dormant? Plan says dormant (predictable);
  flag for revisit after first hands-on session.
