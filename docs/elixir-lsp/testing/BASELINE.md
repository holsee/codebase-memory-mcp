# Checkpoint B — Baseline (before any Elixir change)

Recorded 2026-07-23. Binary built from `main` @ `5e7d3eb7` (worktree
`/tmp/cbm-main`, `scripts/build.sh`, darwin/arm64). Corpus SHAs pinned in
[questions.md](questions.md). Cache isolation: `CBM_CACHE_DIR=/tmp/cbm-eval/baseline`.

## Index run (M5)

Via `scripts/benchmark-index.sh <baseline-binary> elixir <repo> …`:

| Repo | Files | LOC | Index time | Nodes | Edges |
|---|---:|---:|---:|---:|---:|
| plug | 106 | 22,483 | 7,720 ms | 989 | 2,636 |
| phoenix | 479 | 99,358 | 5,338 ms | 3,098 | 8,871 |
| analytics | 2,000 | 699,579 | 5,894 ms | 13,813 | 40,211 |

(Files/LOC counts include non-Elixir files in each repo; phoenix and
analytics carry sizeable JS/TS assets. All M1–M4 below are therefore
**filtered to nodes whose `file_path` ends in `.ex`/`.exs`**.)

## Objective metrics (M1–M4, Elixir files only)

| Metric | plug | phoenix | analytics |
|---|---|---|---|
| M1 def nodes (Function / Class / Module) | 531 / 129 / 72 | 1,351 / 298 / 165 | 4,292 / 1,222 / 1,153 |
| M1b guard-head defs extracted / in corpus | **0 / 102 (0 %)** | **0 / 164 (0 %)** | **0 / 255 (0 %)** |
| M2 CALLS Function-sourced share | 758/1,343 = **56.4 %** | 2,816/4,006 = **70.3 %** | 10,638/17,685 = **60.2 %** |
| M2 raw source-label rows | Fn 758 · Mod 459 · File 126 | Fn 2,816 · Mod 964 · File 226 | Fn 10,638 · Mod 6,305 · File 742 |
| M3 IMPORTS edges | 40 | 82 | 500 |
| M4 CALLS with `lsp_*` strategy | **0** | **0** | **0** |

**Method notes**

- M1b denominator = `grep -rnE '^\s*def(p|macrop?|guardp?|np?)? .* when ' --include='*.ex' --include='*.exs'`
  per repo; numerator = guard-head lines that have a Function/Method node
  with matching `start_line`. Single-line-head approximation (multi-line
  heads undercount the denominator slightly); 0 % is unambiguous — **D1
  confirmed**: no guarded definition produces a node.
- M2: PLAN.md §5.3 predicted ~0 % Function-sourced at baseline; the measured
  56–70 % shows enclosing-function attribution *partially* works today (the
  RED dim-7 repro captures the failing subset). The defect shows up as the
  30–44 % module-sourced remainder (Mod+File rows). Target after PR-0b
  stays > 90 %.
- M4: the unfiltered stores show non-zero `lsp_*` counts on phoenix (132)
  and analytics (525) — those come from **JS/TS assets** resolved by the
  existing ts_lsp; Elixir-sourced `lsp_*` calls are exactly 0, as expected
  (no resolver exists).

## Rubric (M6) — claude -p, model opus

Runner: `claude -p --model opus --mcp-config … --allowedTools "mcp__cbm__*"`,
one fresh invocation per question, ≤ 3 attempts, graded blind against the
keys in [questions.md](questions.md). Full per-question transcripts in
`rubric-baseline/` (summaries below).

**Score: 17/17 PASS (100 %)** — graded blind by separate `claude -p` opus
invocations against the keys (grades + reasons in `grades.csv` alongside the
transcripts; one attempt sufficed for every question).

| Q | Grade | Q | Grade | Q | Grade |
|---|---|---|---|---|---|
| Q1 | PASS | Q7 | PASS | E1 | PASS |
| Q2 | PASS | Q8 | PASS | E2 | PASS |
| Q3 | PASS | Q9 | PASS | E3 | PASS |
| Q4 | PASS | Q10 | PASS | E4 | PASS |
| Q5 | PASS | Q11 | PASS | E5 | PASS |
| Q6 | PASS | Q12 | PASS | | |

### Instrument critique (read before comparing at C1/C2)

The 100 % answer score does **not** mean the graph is complete — it means an
agentic opus client routes around graph gaps. The discriminative baseline
signal is in the *tool paths*:

- **E1**: the graph had **no node for `put_resp_header`** (guarded def, D1).
  The transcript shows a 7-step fallback cascade — `search_graph` (4 pattern
  variants) → `query_graph` (0 rows) → coverage check → `get_code_snippet`
  errors → `search_code` → local `grep`/`awk` — before reconstructing the
  answer from text. The grader credited the (correct) reconstructed content,
  effectively ignoring the "found as a graph node" conjunct of the PASS key.
- Fallback-reference counts per transcript (grep approximation): E1 = 5 with
  3 explicit "no node / 0 rows" signals; Q6/Q11 used `search_code`
  legitimately (text-search questions); everything else answered from graph
  tools directly.
- The answering model used **local Bash/grep** despite the prompt asking for
  MCP tools only — permission inheritance from the local environment.

**Protocol amendments for C1/C2** (runner discipline; the frozen questions
are unchanged):

1. Pass `--disallowedTools "Bash,Read,Grep,Glob"` to `claude -p` so answers
   cannot leak past the MCP surface.
2. Record per-question tool-call counts and fallback cascades alongside
   grades; the improvement claim at C1/C2 rests on (a) objective metrics
   M1–M5, (b) shrinking fallback cascades, (c) grades under the stricter
   runner — in that order of evidential weight.
3. Grader keys with multi-conjunct PASS criteria must be graded
   conjunct-by-conjunct (E1's graph-node clause was leniently ignored here).

## PR-0a spot-check (recorded post-baseline, same day)

After PR-0a (`e18cb5c1`, guard-clause + def-like forms) the candidate binary
re-indexed plug: nodes 989 → 1,030, edges 2,636 → 2,847; **guarded functions
with a graph node 58/82 → 82/82 (100 %)**; guard-head lines 0/102 → 28/102
(remainder collapses into first-clause nodes pending name/arity identity —
D3, PR-0c); `Plug.Conn.put_resp_header` now has a node. Full three-repo
re-measurement happens at checkpoint C1 after all of Phase 0.
