# Archived: dual-capture NAM plugin docs

These documents describe the project's **first incarnation** — a dual-capture stereo NAM plugin —
which was built through milestone M4 and then superseded by the pivot to a block-based plugin
host (2026-07-29). They are kept because:

- The DSP engine they describe (`src/dsp/`, `src/state/`) survives as the built-in NAM block.
- `01-RESEARCH.md` contains verified facts that still apply (NAM Core API, the WHOLE_ARCHIVE
  link requirement, A2 architecture-string findings, measured CPU/latency numbers).
- `04-CAPTURE.md` is the still-valid design for the capture-creation feature, deferred but not
  abandoned.

For the current product, read `docs/1x-*.md` at the docs root.
