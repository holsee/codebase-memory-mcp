# elixir_showcase — pinned Elixir idiom corpus

A deliberately small but idiom-dense Elixir project used as a **deterministic
before/after oracle** for the Elixir Hybrid LSP work (`docs/elixir-lsp/PLAN.md`).
Unlike the external benchmark repos (plug / phoenix / analytics), this one is
tiny, checked in, and pinned, so its numbers don't drift — every module exists
to exercise one or more extraction capabilities.

It is **not** a C test fixture and adds no code to the indexer's test suite.
It is evaluated the same way as the external corpus: index it with a binary
via the CLI and query the resulting graph. See
[`corpus-metrics.sh`](../corpus-metrics.sh) for the before/after runner and
[`AFTER-PHASE-0.md`](../AFTER-PHASE-0.md) for recorded results. (No `mix.exs` —
it is source for the extractor, not a buildable app.)

| File | Idioms exercised | Capability / defect |
|------|------------------|---------------------|
| `lib/accounts/user.ex` | `defstruct`, guarded `def … when`, multi-clause, default arg, `defguard` | D1, D3, D4 |
| `lib/accounts.ex` | `alias`, `alias Foo.{A, B}`, `alias …, as:`, `import`, cross-module + cross-file calls, pipe | D5, D2, Phase 2 |
| `lib/server.ex` | `use GenServer`, callbacks, nested `defmodule`, guarded `handle_*`, `__MODULE__` | D2, D5, D6, Phase 2c |
| `lib/describable.ex` | `defprotocol` + `defimpl` (protocol dispatch) | D4, Phase 2c |
| `lib/math.ex` | `defmacro`, `\|>` pipe chains, capture `&Mod.fun/1`, `require` | D4, Phase 1c |

## What this catches (Phase 0)

Indexed with the pre-Phase-0 binary vs the Phase-0 binary, the graph gains:
guarded/def-like Function nodes that were absent, the nested module qualified
as `Showcase.Server.State` (not bare `State`), a jump in IMPORTS edges from the
now-scanned module bodies, and `defprotocol`/`defimpl` Class nodes. The
capabilities still ahead (arity identity, cross-module call resolution) are
Phase 1–2 and are tracked as the corpus's "not yet" rows.
