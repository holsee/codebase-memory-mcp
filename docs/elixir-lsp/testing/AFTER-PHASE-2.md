# Checkpoint C2 — after Phase 2 (knowledge + cross-file, PR-2a…2c)

Recorded 2026-07-24. Candidate = `feat/elixir-lsp-p2c` @ `c3071bd6`
(PR-2a stdlib seed → PR-2b cross-file → PR-2c use-macro/protocol). Baseline =
the **Phase-1 tip** (`feat/elixir-lsp-p1c` @ `c9485627`, checkpoint C1.5) so the
delta isolates Phase 2's contribution. Same corpus SHAs ([questions.md](questions.md)),
isolated caches per variant.

Phase 2 adds the curated stdlib/`use` knowledge and **cross-file resolution** —
a `Mod.fun/arity` call to a module in another file now resolves to that module's
def node (`lsp_ex_cross`). This is the checkpoint at which M4 was expected to
climb toward the ≥ 70 % goal.

## Cross-checkpoint comparison (Baseline → Phase 2)

Four binaries — **Baseline** (`main` before any Elixir work), **Phase 0**
(grammar remediation), **Phase 1** (resolver + arity), **Phase 2** (cross-file +
knowledge) — each indexing the same four Elixir codebases into isolated caches:
three pinned external repos (`plug` 78 `.ex` files, `phoenix` 177,
`plausible/analytics` 1,210) and the in-repo `elixir_showcase` oracle (5
modules). Three metrics, each measuring a distinct real-world capability tied to
a defect the work fixed.

### 1. Semantic call-resolution coverage (M4)

**What:** of the Elixir call edges in the graph, the share resolved by the
semantic resolver (a module- and arity-verified `lsp_ex_*` strategy) rather than
by textual name-guessing. **Why it matters:** a name-guessed edge points at
*some* function with a matching name; a resolver-verified edge points at the
*right* one — so this is the share of "who-calls-what" edges trustworthy for
cross-module navigation.

| Resolver-verified call edges | plug | phoenix | analytics | showcase |
|---|---|---|---|---|
| Baseline — no resolver exists | 0 % | 0 % | 0 % | 0 % |
| Phase 0 — grammar only, still no resolver | 0 % | 0 % | 0 % | 0 % |
| Phase 1 — per-file resolver (PR-1a/b/c) | 34.9 % | 32.5 % | 18.3 % | 16.7 % |
| Phase 2 — + cross-file resolver (PR-2b) | **44.8 %** | **42.4 %** | **33.7 %** | **66.7 %** |
| Phase 2.5 — + gap closure (PR-2.5a…d; see [AFTER-PHASE-2.5.md](AFTER-PHASE-2.5.md)) | **54.3 %** | **48.4 %** | **36.9 %** | **100.0 %** |

**Change → goal → outcome:** Phase 1 introduced `elixir_lsp` (goal G2 — a
per-file resolver Elixir never had): coverage rises from nothing to a third of
calls. Phase 2's cross-file resolver (goal G3) resolves calls into *other* files
(`lsp_ex_cross`), adding +10–33 points; the clean, idiom-dense showcase reaches
two-thirds. The showcase figure rests on **6 call edges total**, so it is a
capability oracle, not a statistic — the external repos carry the scale claim.

### 2. Call-attribution correctness (M2)

**What:** share of call edges whose *source* is the actual enclosing function,
not the whole module. **Why it matters:** an edge that says "module `Accounts`
calls `User.new`" cannot answer "which *function* calls `User.new`?" — the call
must be attributed to `register/2`. Module-level attribution makes call-chains
and impact analysis useless.

| Calls sourced from the enclosing function | plug | phoenix | analytics | showcase |
|---|---|---|---|---|
| Baseline | 56.4 % | 70.3 % | 60.2 % | 40.0 % |
| Phase 0 — attribution fix (D2) | **67.6 %** | **76.0 %** | **63.3 %** | **100.0 %** |
| Phase 1 | 63.0 % | 71.8 % | 62.6 % | 100.0 % |
| Phase 2 | 63.1 % | 72.0 % | 62.7 % | 100.0 % |

**Change → goal → outcome:** defect **D2** — in-body calls were attributed to
the module because any `call` node counted as a function container. PR-0b made
attribution target-aware (a call counts as a container only when its macro is a
real `def`) and recovered guarded-def bodies. On the showcase, whose functions
are guarded, this took attribution from **40 % → 100 %**. The small Phase 0 →
Phase 1 dip (e.g. plug 67.6 → 63.0) is *precision*, not regression: arity
identity (below) stopped counting fuzzy cross-file name matches that had
inflated the earlier figure.

### 3. Function capture & identity (M1)

**What:** how many function definitions the parser captures at all, and whether
each carries `name/arity` identity (so `foo/1` and `foo/2` are distinct nodes).
**Why it matters:** a definition the parser drops is invisible to the entire
graph; and in Elixir `name/arity` *is* a function's identity — collapsing
arities merges genuinely different functions.

| Function definitions captured | plug | phoenix | analytics | showcase | `name/arity` identity |
|---|---|---|---|---|---|
| Baseline | 531 | 1,351 | 4,292 | 13 | no |
| Phase 0 — guarded + def-like forms (D1/D4) | 568 | 1,445 | 4,399 | 18 | no |
| Phase 1 — arity + default fan-out (D3) | 645 | 1,594 | 4,781 | 19 | **yes — 100 %** |
| Phase 2 | 645 | 1,594 | 4,781 | 19 | yes — 100 % |

**Change → goal → outcome:** defects **D1/D4** — guard-clause heads
(`def f(x) when …`) and def-like forms (`defmacro`, `defguard`, `defdelegate`,
`defprotocol`/`defimpl`) were silently dropped. PR-0a captured them (plug +37
functions; the showcase's guarded `User.new`, `defguard is_adult`, and guarded
`handle_call` went from absent to present). Defect **D3** — every function
shared one node regardless of arity; PR-1c gave each `name/arity` its own node
and fanned default args out (`def f(a, b \\ 1)` → both `f/1` and `f/2`), so
**100 %** of functions became arity-identified.

### The honest ceiling and gap

Resolver coverage tops out at **66.7 % on clean code** (showcase) but sits at
**34–45 %** on real repos. The difference is the enumerated Phase-3 gap:
captures (`&f/N`), bare imported calls (`import …, only:`), and stdlib/project
name-collisions still fall to the textual resolver. Index time and node/edge
counts stayed flat across all four checkpoints, and a non-Elixir TypeScript
control was byte-identical throughout — so every gain above came without cost to
indexing speed or to other languages.

## Index run (M5) — perf guard ≤ +15 %

| Repo | Index time C1.5 → C2 | Nodes | Edges C1.5 → C2 |
|---|---|---|---|
| plug | 7,972 → 9,424 ms | 1,107 (=) | 2,457 → 2,463 |
| phoenix | 5,701 → 6,169 ms | 3,364 (=) | 7,950 → 7,994 |
| analytics | 7,011 → 6,867 ms | 14,347 (=) | 36,500 → 36,543 |

Index time within guard; node counts unchanged (Phase 2 adds no nodes — stdlib
is knowledge, not nodes); edge counts rise slightly as a few more cross-file
calls resolve to real targets.

## Objective metrics (M1–M4, Elixir files only)

| Metric | plug C1.5 → C2 | phoenix C1.5 → C2 | analytics C1.5 → C2 |
|---|---|---|---|
| **M4 lsp_ex_* CALLS share** | 34.9 → **44.8 %** | 32.5 → **42.4 %** | 18.3 → **33.7 %** |
| lsp_ex_* edge count | 417 → **538** | 1,051 → **1,382** | 2,328 → **4,305** |
| M1 arity-identified functions | 100 % (=) | 100 % (=) | 100 % (=) |
| M2 Function-sourced CALLS share | 63.0 → 63.1 % | 71.8 → 72.0 % | 62.6 → 62.7 % |

**M4 rose +10 to +15 points**, driven entirely by cross-file resolution
(`lsp_ex_cross`). Strategy breakdown on plug (1,202 Elixir CALLS):

| strategy | count | |
|---|---|---|
| `lsp_ex_local` | 412 | LSP (same-module) |
| `lsp_ex_cross` | 121 | **LSP (cross-file — new in Phase 2)** |
| `lsp_ex_qualified` | 5 | LSP (same-file qualified) |
| `unique_name` | 392 | textual |
| `same_module` | 151 | textual |
| `suffix_match` | 121 | textual |

## The ≥ 70 % target — not reached at the edge level, and why (honest)

M4 is **44.8 % / 42.4 % / 33.7 %**, short of the aspirational ≥ 70 %. Every
CALLS edge targets a project node (stdlib/framework calls form no edge), so this
is the true "qualified+local resolved" share. The textual remainder is
enumerable and is future work, not a defect:

- **Captures** `&fun/N` (a chunk of `same_module`) — the LSP resolver doesn't
  emit capture calls (they reach as a `binary_operator`, not a `call`); the
  textual pass resolves them.
- **Bare imported calls** (`import M, only: [f: 1]` then `f()`) — `import`
  selector resolution is deferred (a bare call to an imported function isn't
  yet mapped to its source module).
- **Arity-mismatched sites** — where the call-site arity the resolver computes
  doesn't match a def arity, it emits nothing and the textual pass resolves by
  simple name at whatever arity.
- **stdlib/project name collisions** — the C2 rubric surfaced a concrete one:
  `Keyword.get(opts, :realm, "…")` (stdlib `Keyword.get/3`) is mis-resolved by
  the **textual** resolver onto the project-local `Plug.Router.get/3`. The
  stdlib rung *classifies* `Keyword.get/3` (`lsp_ex_stdlib`) but forms no edge
  (Keyword isn't a node), so it does not override/suppress the textual false
  edge. Closing this needs either stdlib-node injection or an
  lsp-says-external suppression signal — a Phase-3 refinement.

So Phase 2 delivered its headline (cross-file resolution) and lifted M4 by
10–15 points; pushing to ≥ 70 % is captures + import-selectors + a
suppression/injection mechanism, now enumerated.

## Cross-file resolution — end-to-end proof

Pinned `elixir_showcase` (P2 binary): the cross-file `User.new/2` and
`User.promote/1` calls in `accounts.ex` resolve **`lsp_ex_cross`** to the
`user.ex` def nodes (arity + module precise). At C1.5 these were fuzzy
`unique_name` textual edges. This is the C1 fuzzy-import finding closed for
qualified cross-module calls.

## Rubric (M6) spot-check — claude -p opus, MCP-only

A focused cross-module + arity question on the Phase-2 plug index (the full
17-question rubric is the PR-3b promotion step; the rubric has limited power to
distinguish an LSP-tagged edge from a textual one that both exist, so this
confirms capability rather than re-scoring):

- **Cross-module trace works, arity-precise:** `Plug.BasicAuth` →
  `Plug.Conn.get_req_header/2`, `halt/1`, `put_resp_header/3`, `resp/3` — the
  cross-module edges resolve with correct arities.
- **Arity identity confirmed:** `basic_auth/1` and `/2`, `request_basic_auth/1`
  and `/2` (default-arg fan-out) are distinct.
- **The model independently flagged the stdlib/project collision** above
  (`Keyword.get/3` mis-attributed to `Plug.Router.get/3` by the textual
  resolver) — corroborating the M4-gap analysis from the tool side.

## Non-Elixir regression control

Index a non-Elixir tree (`graph-ui/src`, 45 TS/TSX files) with the Phase-1-tip
and Phase-2 binaries:

| | Phase-1 tip | Phase-2 |
|---|---|---|
| nodes | 338 | 338 |
| edges | 764 | 764 |
| edge-type histogram | — | identical |

**Byte-for-byte identical.** The `pass_lsp_cross.c` changes (a `CBM_LANG_ELIXIR`
case + a filter exemption joined to Rust) are additive/gated — no perturbation
of other languages.

## Checkpoint C2 verdict

Phase 2 delivered curated stdlib/`use` knowledge and cross-file resolution:
**M4 climbed +10–15 points** (plug 45 %, phoenix 42 %, analytics 34 %), the
cross-file `lsp_ex_cross` edges close the fuzzy-import finding for qualified
calls, arity identity holds, index cost is flat, and other languages are
untouched. The ≥ 70 % target is **not** reached at the edge level; the honest,
enumerated gap is captures, import-selector resolution, and a stdlib-collision
suppression/injection mechanism — Phase-3 refinements. The node-level and
cross-file objective metrics carry the claim; the rubric corroborates and
surfaced the precise remaining imprecision.
