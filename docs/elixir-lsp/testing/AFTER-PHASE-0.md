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

- **M3 (imports)** is the headline: nested `import/alias/require/use` inside
  `defmodule` blocks are now captured (PR-0d). Analytics 500 → 2,064 (4.1×).
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

## Checkpoint C1 verdict

Phase 0 delivered measured structural gains — imports up to 4.1×, call
attribution up across all repos, guard/def-like/protocol forms now extracted —
at flat index cost and zero collateral impact on other languages. The rubric
saturates at the answer level (agentic client), so the honest evidence is the
objective metrics M1–M4 and the shrinking fallback cascades. The headline
capability (arity-precise identity, cross-module resolution) is Phase 1–2.
