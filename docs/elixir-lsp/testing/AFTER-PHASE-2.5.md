# Checkpoint C2.5 — after Phase 2.5 (resolver gap closure, PR-2.5a…2.5d)

Recorded 2026-07-24. Candidate = `feat/elixir-lsp-p25d` @ `c59327f0`
(PR-2.5a local captures → 2.5b import selectors → 2.5c external-call
suppression → 2.5d IMPORTS-edge resolution). Baseline = the **Phase-2 tip**
(`feat/elixir-lsp-p2c`, checkpoint C2) so the delta isolates the gap-closure
work. Same corpus SHAs ([questions.md](questions.md)), isolated caches.

Phase 2.5 closed the four C2 gap items: two unfinished §3 ladder rungs
(`lsp_ex_capture`, `lsp_ex_import`), the rubric-surfaced false-edge defect
(stdlib/project name-collisions), and the open C1 register item (IMPORTS-edge
resolution). This checkpoint verifies each claim empirically, **reporting raw
counts alongside shares** — PR-2.5c removes *false* edges, which shrinks the
denominator, and that must not be mistaken for resolution gains.

## Resolver coverage (M4) — with raw numerator / denominator

Share of Elixir CALLS edges carrying a resolver-verified `lsp_ex_*` strategy.

| Repo | C2 (Phase 2) | C2.5 (Phase 2.5) | numerator Δ | denominator Δ |
|---|---|---|---|---|
| plug | 44.8 % (538 / 1,202) | **54.3 % (608 / 1,119)** | **+70** genuine | **−83** false removed |
| phoenix | 42.4 % (1,382 / 3,262) | **48.4 % (1,421 / 2,938)** | +39 | −324 |
| analytics | 33.7 % (4,305 / 12,786) | **36.9 % (4,473 / 12,119)** | +168 | −667 |
| showcase | 66.7 % (4 / 6) | **100.0 % (6 / 6)** | +2 | 0 |

Two separable effects, both intended:

- **Numerator up** — the new rungs resolve calls that were textual: plug gains
  `lsp_ex_import` 66 and `lsp_ex_capture` 5 (strategy breakdown below).
- **Denominator down** — the suppression (2.5c) removes textual edges that were
  *wrong*: stdlib calls (`Keyword.get/3`, `Enum.map/2`, …) that the registry
  had bound to same-named project functions. These were never correct edges, so
  their removal is a precision gain, and it is reported here as a separate
  effect, not folded silently into the share.

The pinned showcase — clean, idiom-dense code — reaches **6/6 (100 %)**: every
call edge in the oracle is now module- and arity-verified. That is the
resolver's demonstrated ceiling; real repos sit lower because anonymous-fn
captures, dynamic dispatch, and macro-generated calls remain textual or
edge-less by design.

plug strategy breakdown at C2.5 (1,119 CALLS):

| resolver-verified | | textual remainder | |
|---|---|---|---|
| `lsp_ex_local` | 412 | `unique_name` | 269 |
| `lsp_ex_cross` | 120 | `same_module` | 142 |
| `lsp_ex_import` | **66 (new)** | `suffix_match` | 100 |
| `lsp_ex_capture` | **5 (new)** | | |
| `lsp_ex_qualified` | 5 | | |

## The false-edge fix (PR-2.5c) — verified both ways

- **Constructed:** probe `lrp_elixir_s9_stdlib_collision` — a project `get/3`
  plus a call to stdlib `Keyword.get/3` must produce **zero** CALLS edges (the
  only candidate edge is the false one). Fails before 2.5c, passes now.
- **Real-world:** the C2 rubric's false edge (`Plug.BasicAuth.request_basic_auth
  → Plug.Router.get/3`) is **gone** from the plug graph.

## IMPORTS-edge accuracy (PR-2.5d) — the C1 register item, closed

| Oracle | C2 (and every checkpoint since C1) | C2.5 |
|---|---|---|
| showcase | **0** edges from correctly-extracted in-repo imports | **2** edges → exactly `Showcase.Math`, `Showcase.Accounts.User`; stdlib directives (GenServer, Logger, Enum) form no edge |
| plug | 92 edges, **all → the bare root `Plug` node** | **88** edges → the actual modules: `Plug.Conn` ×34, `Plug.Test` ×23, `Plug.Builder`, `Plug.Router`, … |

Probe `lrp_elixir_s10_imports_resolution` pins the behaviour (exactly one edge:
the in-repo alias resolves, the external `require Logger` does not);
`corpus-metrics.sh` now prints IMPORTS **targets** so a mis-pointed edge is
visible, not just counted.

## Cross-checkpoint history (all four codebases)

Resolver-verified share of call edges (M4), Baseline → Phase 2.5:

| | plug | phoenix | analytics | showcase |
|---|---|---|---|---|
| Baseline (no resolver) | 0 % | 0 % | 0 % | 0 % |
| Phase 0 (grammar only) | 0 % | 0 % | 0 % | 0 % |
| Phase 1 (per-file resolver) | 34.9 % | 32.5 % | 18.3 % | 16.7 % |
| Phase 2 (+ cross-file) | 44.8 % | 42.4 % | 33.7 % | 66.7 % |
| **Phase 2.5 (+ gap closure)** | **54.3 %** | **48.4 %** | **36.9 %** | **100.0 %** |

## Guards

- **Index cost:** flat (plug 8.4 → 5.8 s, phoenix 5.8 → 5.9 s, analytics
  7.4 → 7.1 s) — perf guard satisfied.
- **Non-Elixir control:** `graph-ui/src` (45 TS/TSX files) indexes
  **byte-identically** (338 nodes / 764 edges, edge-type histogram equal) under
  the Phase-2 and Phase-2.5 binaries — the `pass_calls`/`pass_parallel`/
  `pass_pkgmap`/`lsp_resolve.h` changes are all Elixir-gated.
- **Full suite:** 6,657 passed / 0 failed / 4 skipped (ASan+UBSan) at the tip.
- **M2 note:** the function-sourced share dips slightly (plug 63.1 → 60.4 %)
  because the suppressed false edges were disproportionately function-sourced —
  the same denominator effect, on M2's numerator. The intra-file attribution
  fixed in Phase 0 is untouched.

## Checkpoint C2.5 verdict

All four gap items closed and verified: captures and imported bare calls now
resolve (`lsp_ex_capture`, `lsp_ex_import` — the last two §3 ladder rungs);
known-external calls can no longer fabricate project edges (constructed +
real-world proof); and IMPORTS edges point at the true declaring modules (the
C1 finding closed at both oracles). The showcase demonstrates the resolver's
ceiling at **100 %**; the real-repo remainder (anonymous captures, dynamic
dispatch, macro-generated calls) is either genuinely dynamic (zero-edge by
design) or textual-but-correct. Phase 3 (hardening + promotion) documents these
as the shipped numbers.
