# Checkpoint C1 — after Phase 0 (grammar-layer remediation, D1–D8)

Recorded 2026-07-24. Candidate binary built from `feat/elixir-hybrid-lsp`
@ `0a25d2f4` (PR-0a…PR-0e). Baseline is checkpoint B (`main` @ `5e7d3eb7`).
Same corpus SHAs ([questions.md](questions.md)), isolated caches
(`/tmp/cbm-eval/baseline` vs `/tmp/cbm-eval/c1`).

## Index run (M5) — perf guard ≤ +15 %

| Repo | Index time B → C1 | Nodes B → C1 | Edges B → C1 |
|---|---|---|---|
| plug | 7,720 → 7,455 ms | 989 → 1,030 | 2,636 → 2,918 |
| phoenix | 5,338 → 5,078 ms | 3,098 → 3,201 | 8,871 → 9,493 |
| analytics | 5,894 → 6,017 ms | 13,813 → 13,940 | 40,211 → 42,920 |

Index time flat (−3 %, −5 %, +2 %); node/edge growth reflects the newly
captured defs/imports. **Perf guard satisfied.**

## Objective metrics (M1–M4, Elixir files only)

| Metric | plug B → C1 | phoenix B → C1 | analytics B → C1 |
|---|---|---|---|
| M1 Function/Method | 531 → **568** | 1,351 → **1,445** | 4,292 → **4,399** |
| M1 Class | 129 → 133 | 298 → 307 | 1,222 → 1,242 |
| M1b guard-head defs (line-anchored) | 0/102 → **28/102** | 0/164 → **69/164** | 0/255 → **89/255** |
| M2 Function-sourced CALLS share | 56.4 → **67.6 %** | 70.3 → **76.0 %** | 60.2 → **63.3 %** |
| M3 IMPORTS(ex) | 40 → **92** | 82 → **223** | 500 → **2064** |
| D7 Variable(ex) | 1 → 1 | 7 → 7 | 110 → 110 |

**Reading the results**

- **M3 (imports) — corrected, read carefully.** PR-0d's real win is import
  *extraction*: nested `import/alias/require/use` are now parsed (proven by
  `test_grammar_imports.c` and the `elixir_alias_forms` unit test). The IMPORTS
  *edge* count rising (40→92 plug, 500→2,064 analytics) looks dramatic but
  **overstates graph value**: the pinned corpus exposed that Elixir import→node
  resolution is *fuzzy* — all 92 of plug's IMPORTS edges resolve to the single
  top-level `Plug` module, not the actually-imported modules (`Plug.Conn`,
  `Plug.Builder`, …). The showcase, which has no bare root module for the
  resolver to collapse onto, produces **0** IMPORTS edges from correctly-extracted
  in-repo imports. So the edge-count delta reflects *more imports fed to a
  fuzzy resolver*, not accurate import edges. Accurate cross-module import
  resolution is Phase 2 (the Hybrid LSP resolver) — see "What the corpus
  surfaced" below. Treat import *extraction* as the Phase-0 deliverable, not the
  IMPORTS edge count.
- **M2 (call attribution)** rose everywhere — guarded-def bodies now source to
  the enclosing function, not the module (PR-0b). The remaining module-sourced
  fraction is expected: module-level calls, macros, and calls whose enclosing
  form is not yet a recognised def. The Hybrid LSP resolver (Phase 1–2) is what
  lifts this toward the > 90 % target.
- **M1b (guard heads, line-anchored)** understates the real gain: it matches a
  node at the exact guard-head line, but multi-clause guarded functions
  collapse to one node under `UNIQUE(project, qualified_name)`, so only one
  clause's line matches. At the *function* level, plug guarded-functions with a
  node went 58/82 → 82/82 (100 %) — the line-anchored 28/102 is the pessimistic
  view; true name/arity identity (one node per clause set) lands in PR-1c (D3).
- **D7 (variables)** held steady: these corpus repos have no top-level bare
  expressions that the old default fallback would have mis-bound, so the
  `=`-only guard changes nothing here — it is proven by the constructed unit
  test `elixir_variable_binding` (fails on main, passes on branch).

## Rubric (M6) — claude -p opus, stricter runner

Per the checkpoint-B protocol amendment, the C1 runner adds
`--disallowedTools "Bash,Read,Grep,Glob,…"` so answers cannot leak past the
MCP surface, and the emphasis is on **tool-path quality** (shrinking
fallback cascades), not just the pass grade. Blind-graded against the frozen
keys.

**Score: 17/17 PASS** under the MCP-only runner (grades in
`rubric-c1/grades.csv`). The pass count matches baseline, but it is a
**stronger** result: baseline reached 17/17 partly via text fallback that the
C1 runner forbids (`--disallowedTools "Bash,Read,Grep,Glob,…"`), so the graph
now answers MCP-only where it previously needed to escape to `grep`.

**Tool-path shift (the real signal):**

| | baseline | C1 |
|---|---|---|
| Questions showing graph-gap signals ("no node" / "0 rows" / "not found") | 3 | **1** |
| Questions that reached for `grep`/`bash` | 2 | **1** (mention only; runner blocks it) |
| **E1** (`put_resp_header/3`, a guarded def) tool path | 7 tools inc. 2×`grep` + `search_code` + `check_index_coverage` (fallback cascade) | **4 graph tools** (`search_graph` → `get_code_snippet` → 2×`trace_path`), no text search |

E1 is the clearest case: on `main` the guarded def had no node, forcing a
7-step cascade ending in local `grep`; at C1 it resolves purely through the
graph. This is exactly the guard-clause fix (PR-0a) plus attribution (PR-0b)
showing up at the tool-use layer.

## Non-Elixir regression control

All Phase-0 changes are Elixir-gated except the shared
`cbm_find_enclosing_func` signature (which gained a `source` parameter; its
per-language logic is unchanged). Control: index a non-Elixir tree
(graph-ui TypeScript) with the baseline and C1 binaries and confirm node/edge
counts are identical.

Control tree: `graph-ui/src` (46 TypeScript/TSX files, 5,782 LOC), indexed
with the baseline binary and the C1 binary into isolated caches.

| | baseline binary | C1 binary |
|---|---|---|
| nodes | 338 | 338 |
| edges | 764 | 764 |
| every label count | — | identical |
| every edge-type count | — | identical |

**Result: byte-for-byte identical.** The shared `cbm_find_enclosing_func`
signature change (added `source`, Elixir-gated logic) is confirmed inert for
non-Elixir languages. Phase 0 is Elixir-scoped as designed.

## Pinned idiom corpus (`corpus/elixir_showcase`)

A small checked-in Elixir project (5 modules, ~35→43 nodes) evaluated the same
way as the external repos — indexed via the CLI, queried with sqlite — so it is
a *deterministic* before/after oracle that doesn't drift and adds no C to the
test suite. Run: `docs/elixir-lsp/testing/corpus-metrics.sh <binary> <cache>`.

| Signal | pre-Phase-0 | Phase 0 |
|---|---|---|
| Functions / Classes | 13 / 5 | **18 / 8** |
| guarded `def User.new/2` node | ✗ | **✓** |
| `defguard User.is_adult` node | ✗ | **✓** |
| guarded callback `handle_call` node | ✗ | **✓** |
| `defmacro Math.const` node | ✓ | ✓ |
| nested module `Showcase.Server.State` | ✗ (bare `State`) | **✓** |
| protocol `Showcase.Describable` | ✗ | **✓** |
| `defimpl … for: User` | ✗ | **✓** |
| `defimpl … for: BitString` | ✗ | **✓** |

**7 of 8 capability checks flip ✗ → ✓** (the 8th, `defmacro`, already worked).
This is the same set of Phase-0 wins the external metrics show, but as a tiny
reproducible fixture. IMPORTS *edges* are intentionally not a headline here: the
corpus's directives target stdlib/undefined modules, and edge formation needs a
resolved in-repo target (Phase 2) — import *extraction* is covered by
`test_grammar_imports.c` and the external plug numbers.

## What the corpus surfaced (findings, not regressions)

Building the pinned corpus did its job — it exposed two things the large-repo
metrics had hidden:

1. **Elixir import resolution is fuzzy (pre-existing; Phase 0 exposed it).**
   `cbm_pipeline_resolve_import_node` (path-based Strategy 1 + namespace-map
   Strategy 2) does not resolve dotted Elixir module names to their declaring
   module node; on plug every import collapses to the root `Plug` node, and on
   the showcase (no root module) nothing resolves. PR-0d did not cause this —
   it only feeds the resolver more (correctly-extracted) imports, making the
   gap visible. This is squarely Phase 2 (cross-module resolution) work; it is
   now a tracked target for the resolver, with the showcase as its oracle.

2. **A reporting overclaim in this document (now corrected).** The first draft
   led with "IMPORTS 40→92 / 500→2064" as a headline Phase-0 win. The count is
   real but the edges are mostly mis-resolved (see M3 above). The defensible
   Phase-0 claims are the *node-level* ones — guarded/def-like Function nodes,
   nested-module QNs, protocol/impl Classes, call-attribution share — all of
   which the corpus confirms 7/8 → 8/8 and which the unit tests gate. Import
   *edge* count has been demoted from headline to caveat.

Neither is a defect in the PR-0a…0e extraction code (unit tests pass; the
node-existence corpus checks are 8/8). Both are exactly what a controlled
before/after oracle is supposed to catch.

## Checkpoint C1 verdict

Phase 0 delivered measured structural gains — guard/def-like/protocol forms now
extracted (corpus 7/8 → 8/8), nested-module QNs joined, call attribution up
across all repos, import *extraction* fixed — at flat index cost and zero
collateral impact on other languages. Two caveats the corpus forced into the
open: the IMPORTS *edge* count rise overstated value (fuzzy resolution — see
above), and the rubric saturates at the answer level (agentic client). So the
honest evidence is the *node-level* objective metrics and the shrinking fallback
cascades, not edge-count deltas. The headline
capability (arity-precise identity, cross-module resolution) is Phase 1–2.
