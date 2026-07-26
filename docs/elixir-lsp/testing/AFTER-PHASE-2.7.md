# Checkpoint C2.7 — after Phases 2.6 + 2.7 (precision + capability passes)

Recorded 2026-07-24. Candidate = `feat/elixir-lsp-p27d` tip (PR-2.6a…PR-2.7d).
Baseline = the **Phase-2.5 tip** (`feat/elixir-lsp-p25d`, checkpoint C2.5).
Same corpus SHAs, isolated caches, raw counts alongside shares.

Phase 2.6 closed the two remaining false-edge classes (Erlang atom-module and
uncurated-dep collisions) and the selector atom forms; Phase 2.7 added
`__MODULE__` aliases, defdelegate edges, behaviour OVERRIDE linkage, opt-in
stdlib nodes, and defstruct Struct nodes.

## Resolver coverage (M4) — raw numerator / denominator

| Repo | C2.5 | C2.7 | numerator Δ | denominator Δ |
|---|---|---|---|---|
| plug | 54.3 % (608 / 1,119) | **56.8 % (610 / 1,073)** | +2 (delegates) | −46 (atom/dep false edges) |
| phoenix | 48.4 % (1,421 / 2,938) | **52.0 % (1,426 / 2,742)** | +5 | −196 |
| analytics | 36.9 % (4,473 / 12,119) | **39.0 % (4,492 / 11,517)** | +19 | −602 |
| showcase | 100 % (6 / 6) | **100 % (6 / 6)** | — | — |

The denominator drops are the Phase-2.6 suppressions removing **false** edges
(the plug removals were individually reviewed at PR-2.6b: 47 removed, 0 added,
every one a dep/stdlib/atom-module mis-binding). M2 dips ~2 pts for the same
reason (the removed false edges were function-sourced) — the documented
denominator effect, not an attribution regression.

**With Phases 2.6a+2.6b landed, every known systematic false-edge class is
closed**: Elixir stdlib (2.5c), Erlang atom-modules (2.6a), uncurated hex deps
(2.6b) — each pinned by a collision probe asserting **zero** CALLS
(S9/S11/S12).

## New graph capabilities (showcase oracle, C2.5 → C2.7)

| | C2.5 | C2.7 |
|---|---|---|
| INHERITS (module → behaviour) | 0 | **1** (`Showcase.Server` → `GenServer`) |
| OVERRIDE (callback → behaviour identity) | 0 | **3** (`init/1`, `handle_call/3`, `handle_cast/2`) |
| Struct nodes (defstruct/defexception) | 0 | **2** (`User`, `Server.State`) |
| IMPORTS resolved | 2 | **3** (`use GenServer` now also resolves — the behaviour Class exists as a node) |
| Resolver-verified CALLS | 6/6 | 6/6 |

Probes: S13 (defdelegate → one real delegator→target edge), S14 (INHERITS ≥ 1
+ OVERRIDE ≥ 2 via `use GenServer`), S15 (opt-in stdlib nodes: a lone
`Enum.map` call forms a CALLS edge with `CBM_ELIXIR_STDLIB_NODES`, 0 without).

## Cross-checkpoint history (resolver-verified call-edge share)

| | plug | phoenix | analytics | showcase |
|---|---|---|---|---|
| Baseline / Phase 0 | 0 % | 0 % | 0 % | 0 % |
| Phase 1 | 34.9 % | 32.5 % | 18.3 % | 16.7 % |
| Phase 2 | 44.8 % | 42.4 % | 33.7 % | 66.7 % |
| Phase 2.5 | 54.3 % | 48.4 % | 36.9 % | 100 % |
| **Phase 2.7** | **56.8 %** | **52.0 %** | **39.0 %** | **100 %** |

## Guards

- **Non-Elixir control:** graph-ui TS byte-identical (338 nodes / 764 edges)
  under both binaries — every 2.6/2.7 change is language- or extension-gated
  (incl. the `pass_semantic`/`pass_pkgmap` additions).
- **Index cost:** flat across all repos.
- **Full suite:** 6,673 passed / 0 failed / 4 skipped at the tip; 21 new tests
  across the two passes (S9–S15 probes + unit coverage).
- **Node counts:** showcase 44 → 54 (+GenServer behaviour Class, +7 callback
  identities — conditional on actual `use` — and +2 Structs); repos gain only
  behaviour/struct nodes where used (plug +42, phoenix +48, analytics +40).

## Verdict

The precision pass finished the false-edge work (zero known fabricated-edge
classes remain); the capability pass made behaviours, delegations, and data
shapes first-class graph citizens and offered stdlib edges as an explicit
opt-in. Remaining gaps are the recorded future extensions (`.heex`/`.eex`
first) and the inherent static-analysis limits. Phase 3 (hardening +
promotion) documents these as the shipped numbers.
