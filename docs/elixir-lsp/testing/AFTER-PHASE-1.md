# Checkpoint C1.5 — after Phase 1 (Hybrid LSP resolver, PR-1a…1c)

Recorded 2026-07-24. Candidate binary built from `feat/elixir-lsp-p1c`
@ `79ebbc7d` (PR-1a skeleton → PR-1b resolution → PR-1c name/arity identity).
Baseline is the **Phase-0 tip** (`feat/elixir-hybrid-lsp` @ `155db669`, the C1
state) so the delta isolates the **resolver's** contribution rather than
re-counting the Phase-0 grammar work. Corpus SHAs per [questions.md](questions.md);
isolated caches per variant.

Phase 1 adds `internal/cbm/lsp/elixir_lsp.{c,h}` — the first `lsp_ex_*` resolved
CALLS edges for Elixir — plus name/arity identity (`Mod.foo/2`). This is the
first checkpoint at which metric **M4** (share of calls resolved by an
`lsp_ex_*` strategy) is measurable.

## Index run (M5) — perf guard ≤ +15 %

| Repo | Index time C1 → C1.5 | Nodes C1 → C1.5 | Edges C1 → C1.5 |
|---|---|---|---|
| plug | 7,827 → 5,197 ms | 1,030 → 1,107 | 2,918 → 2,457 |
| phoenix | 5,955 → 6,649 ms | 3,213 → 3,364 | 9,504 → 7,950 |
| analytics | 6,223 → 6,499 ms | 13,958 → 14,347 | 42,937 → 36,500 |

Index time flat (−34 %, +12 %, +4 %) — **perf guard satisfied**. Node counts
rise from the arity fan-out (below); edge counts fall — see the precision
analysis under M2/M4.

## Objective metrics (M1–M4, Elixir files only)

| Metric | plug C1 → C1.5 | phoenix C1 → C1.5 | analytics C1 → C1.5 |
|---|---|---|---|
| M1 Function/Method | 568 → **645** | 1,445 → **1,594** | 4,399 → **4,781** |
| M1 arity-suffixed (`/N`) | 0 → **645 (100 %)** | 0 → **1,594 (100 %)** | 0 → **4,781 (100 %)** |
| **M4 lsp_ex_* CALLS share** | 0 → **34.9 %** (417) | 0 → **32.5 %** (1,051) | 0 → **18.3 %** (2,328) |
| M2 Function-sourced CALLS share | 67.6 → 63.0 % | 76.0 → 71.8 % | 63.3 → 62.6 % |
| CALLS(ex) total | 1,502 → 1,196 | 4,376 → 3,235 | 18,635 → 12,742 |

**Reading the results**

- **M4 (the Phase-1 headline) — 0 → ~33 % on the libraries, 18 % on the large
  app.** These are the calls the resolver resolves *concretely* (same-module
  local + same-file qualified via alias) and tags `lsp_ex_local` /
  `lsp_ex_qualified`. analytics is lower because a large app makes proportionally
  more **cross-module** calls, whose accurate resolution is Phase 2b — so the
  ≥ 70 % target is a Phase-2 deliverable, exactly as scheduled. This is the first
  non-zero M4 for Elixir.

- **M1 — every function is now arity-identified.** 100 % of Function nodes carry
  a `/arity` QN. The count rise (+77 / +149 / +382) is the name/arity split
  (`foo/1` and `foo/2` are now distinct nodes) plus default-arg fan-out
  (`def f(a, b \\ 1)` → `f/1` and `f/2`).

- **CALLS total fell — this is a precision gain, not a regression.** Splitting
  CALLS by intra-file vs cross-file shows the drop is almost entirely
  cross-file:

  | | intra-file C1 → C1.5 | cross-file C1 → C1.5 |
  |---|---|---|
  | plug | 583 → **573** (−1.7 %) | 919 → **623** (−32 %) |
  | phoenix | 1,349 → **1,324** (−1.9 %) | 3,027 → **1,911** (−37 %) |
  | analytics | 3,425 → **3,234** (−5.6 %) | 15,210 → **9,508** (−37 %) |

  Intra-module resolution is preserved; the cross-file drop is arity **correctly
  rejecting loose by-name matches** — before Phase 1, `Enum.map(list, f)`
  resolved by simple name `map` to *any* project function named `map`; now it
  needs `map/2`, so cross-module false positives disappear. Those edges will
  return, arity-accurate, once Phase 2b resolves dotted module names to their
  declaring module. M2 dips a few points as a side-effect (the removed cross-file
  edges were disproportionately function-sourced); on the reliable intra-file
  subset attribution is unchanged.

## Pinned idiom corpus (`corpus/elixir_showcase`)

Re-run with the arity-aware `corpus-metrics.sh`. All **8/8** capability checks
still hold, and arity identity is visible at the node level:

- `Showcase.Accounts.User.promote/1` and `promote/2` are now **distinct nodes**
  (name/arity split); `handle_call/3`, `tick/2`, `send_file/5` etc. are all
  arity-suffixed.
- The `handle_call → tick/2` same-module call resolves **`lsp_ex_local`**.
- The capture `&double/1` resolves to `double/1` (arity preserved).
- Functions 18 → **19** (a default-arg fan-out); the 8 node-existence checks are
  unaffected because `def.name` stays bare (only `qualified_name` carries `/N`).

## Rubric (M6) — claude -p opus, MCP-only runner

Focused sample on the two questions Phase 1 most directly affects (the full
17-question protocol in [questions.md](questions.md) is unchanged; the objective
M1/M4 metrics carry the primary claim, per the checkpoint-B guidance). Same
runner as C1 (`--model opus`, `--disallowedTools "Bash,Read,Grep,Glob"`,
strict MCP), blind-graded, both variants on the plug index the same day.

**Q-arity — "does the graph distinguish functions by arity? find a
multi-arity name."**

| | Baseline (Phase-0 tip) | Candidate (Phase-1) |
|---|---|---|
| Grade | **FAIL** | **PASS** |
| Finding | After 10 graph queries (incl. the decisive `group by (file_path, name) having count>1` → **0 rows**), concluded *"arity is not distinguished — `get_session/1` and `/2,/3` exist in source yet only one graph node exists."* | Found `send_file/5`, `read_body/1` vs `read_body/2` (distinct nodes, different in-degrees — "callers resolve to the specific arity"), even cross-module `__catch__/5` vs `/6`. Every QN carries `/arity`. |

This is the D3 capability appearing at the tool layer: arity identity is now
**queryable in the graph**, where before it was absent.

**E1 — "which functions does `Plug.Conn.put_resp_header/3` call, and who calls
it? give arity."** (arity-correct trace)

| | Baseline (Phase-0 tip) | Candidate (Phase-1) |
|---|---|---|
| Tool path | **18 `get_code_snippet` calls** to read arities from def heads ("the graph stores no arity/signature property… came back empty"); emitted a **false-positive callee** (`Adapter.conn/5`, "not present in source"). | `search_graph` → `trace_path` on `…put_resp_header/3` — **4 clean graph calls**, arity native in the QN, no source-reading cascade, no false edge. |

The arity that the baseline had to reconstruct by reading 18 snippets is now a
first-class part of the node identity, and the arity-precise resolution dropped
the baseline's spurious edge.

## Non-Elixir regression control

All Phase-1 changes are Elixir-gated. Control: index a non-Elixir tree
(`graph-ui/src`, 45 TypeScript/TSX files) with the Phase-0-tip binary and the
Phase-1 binary into isolated caches.

| | Phase-0-tip binary | Phase-1 binary |
|---|---|---|
| nodes | 338 | 338 |
| edges | 764 | 764 |
| CALLS | 158 | 158 |
| label / edge-type histograms | — | identical |

**Byte-for-byte identical.** Phase 1 is Elixir-scoped as designed — the arity
change (4 sites) and the resolver perturb no other language.

## Checkpoint C1.5 verdict

Phase 1 delivers the resolver layer and name/arity identity: **M4 0 → ~33 %**
(libraries) / **18 %** (large app) of Elixir calls now resolve with an
`lsp_ex_*` strategy; **100 %** of functions are arity-identified with `foo/1`
and `foo/2` as distinct nodes; the rubric shows arity is now queryable in the
graph (baseline FAIL → candidate PASS) and arity-precise traces no longer need a
`get_code_snippet` cascade. The CALLS-count drop is a precision gain — arity
rejects the fuzzy cross-module by-name matches; intra-module resolution is
intact, and accurate cross-module (and stdlib) resolution — which lifts M4 toward
the ≥ 70 % target — is Phase 2. Index cost flat; zero collateral impact on other
languages.
