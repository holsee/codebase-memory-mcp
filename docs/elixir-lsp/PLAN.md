# Elixir Hybrid LSP — Execution Plan

**Status:** In progress · **Branch:** `feat/elixir-hybrid-lsp` (fork `holsee/codebase-memory-mcp`) · **Tracking issue:** [holsee/codebase-memory-mcp#1](https://github.com/holsee/codebase-memory-mcp/issues/1)
**Last updated:** 2026-07-24

This document is the single source of truth for bringing Elixir to full Hybrid
LSP parity with the ten resolver-backed languages (Python, TypeScript, Go,
Rust, Java, Kotlin, PHP, C#, C/C++, Perl). It is written to be self-contained:
a contributor (or a fresh working session) should be able to execute any phase
from this document plus the referenced files alone.

**Read order for a new session:**

1. This §1 Primer (context and file map).
2. §6 Progress tracker — find the first unchecked item; that is the work.
3. The phase section for that item (§4) and its acceptance criteria.
4. §5 Test & evaluation protocol before making any change that claims a
   measurable improvement — baselines must exist before the change lands.

---

## 1. Primer

### 1.1 What this project is

codebase-memory-mcp is a pure-C MCP server that indexes repositories into a
SQLite-backed code knowledge graph (nodes: Function/Class/Module/…; edges:
CALLS/IMPORTS/DEFINES/…). Language support has two layers:

1. **Tree-sitter extraction** (`internal/cbm/`) — syntactic pass over every
   file: definitions, calls, imports, variables. Spec-driven via node-type
   tables in `lang_specs.c`, plus per-language special cases in `extract_*.c`.
2. **Hybrid LSP** (`internal/cbm/lsp/`) — per-language semantic resolvers that
   refine call edges using import maps, scopes, and type registries. A
   resolver emits `CBMResolvedCall {caller_qn, callee_qn, strategy,
   confidence}` records; the pipeline overrides textual CALLS edges with them
   (confidence floor 0.6, `src/pipeline/lsp_resolve.h:36`).

Elixir today has layer 1 only, with known defects (§1.3). This plan fixes
layer 1, then adds a layer-2 resolver.

### 1.2 Integration contract (file map)

| Concern | Location |
|---|---|
| Language enum (`CBM_LANG_ELIXIR` exists) | `internal/cbm/cbm.h:29` |
| Elixir node-type spec | `internal/cbm/lang_specs.c:508-513`, row at `:1751-1754` |
| Elixir def/module extraction | `internal/cbm/extract_defs.c:4532-4637` |
| Elixir callee extraction | `internal/cbm/extract_calls.c:432-442` |
| Elixir QN/scope computation | `internal/cbm/extract_unified.c:160-166, 532-535` |
| Import parsing (generic, root-only) | `internal/cbm/extract_imports.c:2792-2795`, `parse_generic_imports` `:943-961` |
| Enclosing-function kinds | `internal/cbm/helpers.c:673` (`func_kinds_elixir`), `:990` |
| Phoenix channels/PubSub (works today) | `internal/cbm/extract_channels.c:875-970` |
| Phoenix/Oban/Broadway service patterns (work today) | `internal/cbm/service_patterns.c:236-239, 374` |
| Per-file LSP dispatch | `internal/cbm/cbm.c:1228-1274` (add one `if` block) |
| Unity build for resolvers | `internal/cbm/lsp_all.c` (add two `#include`s) |
| Cross-file LSP opt-in switch | `src/pipeline/pass_lsp_cross.c:404-421` |
| Cross-file per-file-build dispatch | `src/pipeline/pass_lsp_cross.c:547-594` |
| Shared LSP infra (reuse, don't reinvent) | `internal/cbm/lsp/type_registry.{c,h}`, `type_rep.{c,h}`, `scope.{c,h}`, `lsp_neg_memo.h`, `lsp_node_iter.h` |
| Stdlib seed data | `internal/cbm/lsp/generated/<lang>_stdlib_data.c` (hand-curated is fine; Perl's is 140 lines) |
| Test registration (5 points) | `Makefile.cbm:415-442, 535`; `tests/test_main.c` extern + `RUN_SELECTED_SUITE` |
| Per-language LSP probe matrix | `tests/test_lsp_resolution_probe.c` (8 scenarios per language) |
| Known-classes matrix | `tests/test_matrix_known_classes.c` (Elixir has zero cases) |
| Originality manifest | `scripts/check-lsp-originality.sh` `REFS[]` |
| Release benchmark matrix | `MAINTAINERS.md` (Hybrid LSP release benchmark matrix) |
| Benchmark corpus / harness | `scripts/clone-bench-repos.sh` (`plausible/analytics` listed), `scripts/benchmark-index.sh`, methodology `docs/BENCHMARK.md` |

The reference implementation pattern for a dynamic language is the Perl
resolver (`internal/cbm/lsp/perl_lsp.{c,h}`, ~1.6k lines), including its
**zero-edge guarantee**: an unresolvable receiver emits *no* edge rather than
a guessed one. The staged delivery precedent is the Perl commit arc
(skeleton → wiring → stdlib seed → semantic core → tests → QA rounds).

### 1.3 Current Elixir defects (grammar layer)

tree-sitter-elixir parses `def`, `defp`, `defmodule`, etc. as plain `call`
nodes; all extraction rests on target-keyword matching in
`extract_elixir_call`. Verified defects, in priority order:

| # | Defect | Evidence |
|---|---|---|
| D1 | Guard-clause defs dropped: `def foo(x) when x > 0` has a `binary_operator` first argument; only `call` and `identifier` are handled | `extract_defs.c:4545-4551` |
| D2 | Enclosing-function attribution broken: `func_kinds_elixir = {"call"}` cannot distinguish a `def` call from any call, so in-body calls attach to the **module** QN | `helpers.c:673`; RED dimension in `tests/repro/repro_grammar_functional.c:255` |
| D3 | No name/arity identity: every clause of a multi-clause function emits a duplicate Function def; Elixir identity is `name/arity` | `extract_defs.c:4556-4565` |
| D4 | `defprotocol`, `defimpl`, `defstruct`, `defguard(p)`, `defdelegate`, `defexception`, `defmacrop`, `defn(p)` not extracted | `extract_defs.c:4621-4624` keyword list |
| D5 | Imports nested in `defmodule do…end` invisible: `parse_generic_imports` scans root children only, and idiomatic `import`/`alias`/`require`/`use` live inside the module block; the four directives are also not semantically distinguished | `extract_imports.c:943-961, 2792-2795` |
| D6 | Nested module QNs not joined (`defmodule Foo do defmodule Bar` emits `Bar`, not `Foo.Bar`) | `extract_defs.c:4569-4595` |
| D7 | Variable extraction matches every `binary_operator` (`a + b`, `a == b` become variables) | `lang_specs.c:513` |
| D8 | Weak test contracts: Elixir regression asserts `min_defs=1` with no name verification | `tests/test_grammar_regression.c:148` |

### 1.4 Design references (clean-room)

Inspiration sources — **algorithms and data models only; no code may be
copied or closely paraphrased**. Before any resolver PR merges, add the rows
below to `REFS[]` in `scripts/check-lsp-originality.sh` and run it clean:

- **Expert** (official Elixir LS; local clone `~/workspace/_oss/expert`;
  github.com/expert-lsp/expert). Its pure-static `forge` layer proves which
  facts are statically computable. Transferable designs:
  - Scope = `{range, module_segments, aliases, imports, requires, uses}`;
    alias = `{module_segments, as_segments}`.
  - Alias visibility by line (aliases at lines ≤ call site, later overrides).
  - Transitive alias chaining (`alias A.B` then `alias B.C` → `A.B.C`).
  - Implicit aliases: `__MODULE__` on `defmodule`; `@protocol`/`@for` on
    `defimpl`; nested short-name aliases prefixed by the enclosing module.
  - Default-arg arity fan-out: `def foo(a, b \\ 1)` defines `foo/1` AND
    `foo/2` (`min_arity = arity − count of \\ nodes`).
  - Pipe adds +1 arity: `x |> foo(y)` resolves `foo/2`.
  - Captures `&Mod.fun/2` / `&fun/1` matched explicitly, inner call skipped
    (avoids a spurious `fun/0` reference).
  - `defdelegate` records the delegated-to module/fun/arity.
  - Expert delegates `use` expansion, struct-field reflection, and
    import-export checks to the running BEAM → a static engine must supply
    curated tables for these (§4 Phase 2c).
- **ElixirLS / elixir_sense, Lexical, Next LS** — Expert's ancestors; list in
  `REFS[]` for completeness.
- **tree-sitter-elixir `queries/tags.scm`** (elixir-lang org) — canonical
  syntactic recognition of definitions: match `call` nodes by target keyword
  with **three** argument shapes (bare identifier = zero-arity; `call` =
  normal; `binary_operator` = `when`-guarded), keyword set includes
  `defmacrop`, `defn`, `defnp`, `defguard(p)`, `defdelegate`.

### 1.5 Goals, non-goals, success criteria

**Goals**

- G1: Fix all grammar-layer defects D1–D8.
- G2: Ship `internal/cbm/lsp/elixir_lsp.{c,h}` — per-file resolver with
  alias/import scoping, arity-keyed resolution, pipe/capture handling.
- G3: Cross-file resolution (fallback tier, Kotlin-style dispatch).
- G4: Curated knowledge: Elixir core stdlib seed + `use` macro expansion
  table (GenServer, Supervisor, Phoenix.Controller/LiveView, Ecto.Schema…).
- G5: Empirically demonstrate improvement on real repositories (§5), and add
  Elixir to the Hybrid LSP release benchmark matrix and README table.

**Non-goals**

- Full macro expansion (`quote`/`unquote` evaluation). Curated `use` table
  only; unknown macros fall through to the textual resolver.
- Dialyzer-grade typespec inference. `@spec` is parsed as a *hint* only, and
  only in Phase 3 if metrics justify it.
- Ruby (tracked separately; this plan is Elixir-first by decision).

**Success criteria**

- All 8 defects have regression tests that fail on `main` and pass on the
  branch.
- `tests/repro/repro_grammar_functional.c` Elixir dimension 7 flips RED→GREEN.
- ≥ 70 % of same-project qualified calls (`Mod.fun(...)`) and local calls in
  the corpus resolve with an `lsp_*` strategy (measured per §5.3 M4).
- Rubric score (§5.4) improves from baseline, targeting ≥ 90 % (Excellent
  tier) on the Elixir corpus.
- Zero-edge guarantee holds: no resolved edge for dynamic dispatch
  (`apply/3`, variable modules, unknown macros) — negative tests enforce.
- All gates green: `scripts/test.sh` (ASan/UBSan), `make test-tsan`,
  `scripts/lint.sh`, `make -f Makefile.cbm security`,
  `scripts/check-lsp-originality.sh`.

---

## 2. Conventions this work must follow

From `CONTRIBUTING.md` and observed practice — every PR in this plan:

1. **References the tracking issue** (`Closes #N` / `Fixes #N`). Open the
   issue *before* the first code PR; new pipeline passes / indexing
   algorithms explicitly require prior design discussion — this document is
   the design artefact to link.
2. **Stays small and single-purpose** — target < 500 lines per PR; the phase
   breakdown in §4 is sized accordingly. Never mix a fix with a feature.
3. **Conventional commits with DCO sign-off**: `feat(elixir-lsp): …`,
   `fix(elixir): …`, `test(elixir-lsp): …` — always `git commit -s`.
4. **C only**, arena-allocated (`CBMArena`), no new dependencies. Any new
   `system()`/`popen()`/network call needs `scripts/security-allowlist.txt`
   justification (none is anticipated).
5. **Gates before every push**: `scripts/test.sh` and `scripts/lint.sh`
   minimum; TSan and the security audit before requesting review. Code must
   be portable to mingw-w64-clang incl. ARM64 Windows (no ASan there) — the
   resolver is pure computation, so this is a style constraint, not a design
   one.
6. **Comment/naming idiom**: follow `perl_lsp.c` — file-header design block,
   `elixir_`-prefixed statics, `cbm_`-prefixed public API, docstring-style
   section rules (`/* ── Section ─── */`).
7. **Test suite wiring is five points** (Makefile var, `ALL_TEST_SRCS`,
   extern, `RUN_SELECTED_SUITE`, and the shard union guard verifies it).
8. **Reproducible environment**: `.devcontainer/` provides the full
   toolchain at CI-pinned versions (clang-format-20, clang-tidy-20,
   cppcheck 2.20.0, Node 22, sqlite3, Claude CLI for §5.4). Prefer working
   inside it; the pre-commit hook then has everything it needs.

---

## 3. Architecture of the resolver (design summary)

`elixir_lsp` follows the Perl per-file skeleton with an Elixir-specific
semantic core. Per file:

1. **Scope walk.** Iterative AST walk (use `cbm_lsp_collect_children`)
   building a scope stack: `defmodule` pushes a module scope (joined QN per
   D6); `def`/`defp`/`defmacro(p)`/`defguard(p)`/`defn(p)` push a function
   scope keyed `name/arity` (default-arg fan-out registers every arity in
   `min..max`); stab clauses and `do/else/rescue/catch/after` blocks push
   block scopes.
2. **Directive collection.** Within each scope, record `alias` (incl.
   multi-alias `Foo.{Bar, Baz}`, `as:`, `__MODULE__.Sub`), `import` (with
   `only:`/`except:` name/arity selectors), `require as:`, and `use`
   (module + opts, resolution deferred to the curated table). Aliases are
   line-ordered; resolution consults only those at lines ≤ the call site.
3. **Call resolution ladder** (each rung emits `strategy`, confidence):
   a. Qualified `Mod.fun(args)` → expand alias chain → registry lookup by
      QN + arity (`lsp_ex_qualified`).
   b. Pipe `x |> Mod.fun(a)` → rung (a) with arity+1 (`lsp_ex_pipe`).
   c. Local `fun(args)` → current module's defs, then imports (selector- and
      arity-checked), then curated `use`-injected functions, then Kernel
      auto-import (`lsp_ex_local` / `lsp_ex_import` / `lsp_ex_use` /
      `lsp_ex_kernel`).
   d. Capture `&Mod.fun/2`, `&fun/1` → direct arity form (`lsp_ex_capture`).
   e. `__MODULE__.fun(...)`, `@protocol`/`@for` inside `defimpl` → implicit
      alias resolution.
   f. Anything dynamic (`apply/3`, variable module, `unquote`) → **no edge**.
4. **Output.** `cbm_resolvedcall_push` with confidence ≥ 0.6 only for rungs
   with a concrete registry hit; caller QN comes from the (fixed) enclosing
   function scope.

Cross-file (Phase 2b): project-wide def registry filtered to
`CBM_LANG_ELIXIR` defs (`CBMLSPDef.lang`), keyed `Module.fun/arity`, via the
fallback-tier dispatch (`cbm_run_elixir_lsp_cross`), mirroring Kotlin's
wiring. Tier-2 prebuilt registry is a Phase 3 option, decided on measured
index-time cost.

Curated knowledge (Phase 2c): `generated/elixir_stdlib_data.c` seeds Kernel,
Enum, Map, String, List, Keyword, Process, GenServer, Supervisor, Task, Agent
signatures (name/arity; return types largely `unknown` — arity-correct
resolution is the goal, matching the Perl precedent). A static `use` table
maps `use GenServer` → injected callables/callbacks, `use Phoenix.Controller`,
`use Phoenix.LiveView`, `use Ecto.Schema`, `use ExUnit.Case` — each entry:
trigger module, functions injected into local scope, expected `@callback`
names (emitted as OVERRIDE-style resolution targets for `handle_call` etc.).

---

## 4. Phased execution plan

Each phase lists its PR-sized work items with acceptance criteria (AC).
Estimated total: ~5–8k lines across ~10 PRs.

### Phase 0 — Grammar-layer remediation (prerequisite)

> The resolver's `caller_qn` is worthless until D1–D3 are fixed. Do these
> first, in order.

**PR-0a `fix(elixir): extract guard-clause and remaining def-like forms`**
(D1, D4 def forms)
- `extract_elixir_func_def`: handle `binary_operator` first-arg (descend to
  the `call`/`identifier` left of `when`); extend keyword set with
  `defmacrop`, `defguard`, `defguardp`, `defn`, `defnp`, `defdelegate`
  (delegate records target in metadata where the def struct allows),
  `defexception` (emit struct-like Class), `defprotocol` (Class, like
  defmodule), `defimpl` (Class named `Protocol.For.Target`), `defstruct`
  (Field defs under current module, if Field emission is cheap; else record
  as metadata for Phase 2).
- AC: new cases in `tests/test_extraction.c` — guarded def, multi-head def,
  each new form; all fail on `main`.

**PR-0b `fix(elixir): attribute in-body calls to the enclosing function`** (D2)
- Make `cbm_find_enclosing_func` target-aware for Elixir: a `call` node
  counts as a function container only when its target identifier is in the
  def-keyword set (the tags.scm approach). Adjust
  `compute_elixir_func_qn` / `func_kinds_elixir` accordingly.
- AC: `repro_grammar_functional.c` Elixir dim 7 RED→GREEN (update the file's
  expectation and header comment); a `test_lang_contract.c`-style case
  asserting caller QN is the function, not the module.

**PR-0c `fix(elixir): nested module QNs`** (D6) — **scope narrowed; see below**
- Join nested `defmodule`/`defimpl` QNs with the enclosing module path
  (`defmodule Foo do defmodule Bar` → `Foo.Bar`) via an AST ancestor walk,
  mirroring the class-chain logic in `cbm_enclosing_func_qn`.
- AC: extraction test `elixir_nested_module` (fails on main, passes here).

> **D3 (name/arity identity) deferred to PR-1c — decided by the fable
> consumer-impact analysis (2026-07-23), recorded in the log.** Arity-in-QN
> (`Mod.foo/2`) is the correct identity model and the store already tolerates
> `/` in QNs, but it **cannot land without the callee side** also emitting
> `foo/2` — the CALLS resolver keys on `simple_name(qualified_name)` on both
> the def and call-site ends (`registry.c`), so a def-side-only arity suffix
> silently breaks every intra-module Elixir CALLS edge. The def QN, the
> enclosing-caller QN, and the callee arity must change together, all
> Elixir-gated. Since PR-1c computes call-site arity anyway (pipes, captures,
> defaults), D3 moves there and lands atomically. Multi-clause span-merge also
> moves to PR-1c: it needs module-qualified function QNs (else same-named
> private helpers in different modules collide file-wide), which is part of
> the same symmetric change. The store's `UNIQUE(project, qualified_name)`
> already collapses multi-clause heads to one node today, so the only interim
> loss is line-span precision — acceptable until PR-1c.

**PR-0d `fix(elixir): scan module-body directives and classify them`** (D5)
- Extend import parsing to walk `defmodule` do-blocks (and nested modules);
  emit IMPORTS edges tagged by directive kind (`alias`/`import`/`require`/
  `use`) — the graph edge type stays IMPORTS; kind goes in edge metadata.
- AC: `test_grammar_imports.c` fixture rewritten to the idiomatic nested
  form (this currently passes only because the fixture is unidiomatic);
  counts for all four directives.

**PR-0e `fix(elixir): variable extraction + contract strengthening`** (D7, D8)
- Restrict var extraction to `=` match operators (operator field check).
- Strengthen `test_grammar_regression.c` Elixir entry to name-verified defs
  (parity with Ruby's entry).
- AC: no spurious Variable nodes for `a + b` fixtures; regression contract
  at Ruby's strength.

*Baseline measurement (§5) must be recorded **before PR-0a merges**.*

### Phase 1 — Resolver skeleton and wiring

**PR-1a `feat(elixir-lsp): resolver skeleton, dispatch, stdlib stub, test suite scaffold`**
- `elixir_lsp.h` (context struct mirroring `PerlLSPContext`),
  `elixir_lsp.c` (init/free + no-op walk), `generated/elixir_stdlib_data.c`
  stub, dispatch `if` in `cbm.c:~1240`, two `#include`s in `lsp_all.c`,
  `tests/test_elixir_lsp.c` scaffold + five-point wiring, `REFS[]` rows in
  `check-lsp-originality.sh`.
- AC: builds clean on all gates; empty suite runs; originality scan green.

**PR-1b `feat(elixir-lsp): scopes, aliases, imports, local/qualified resolution`**
- §3 steps 1–2 and ladder rungs (a) and (c) minus `use`; Kernel auto-import
  from the stdlib seed.
- AC: test cases — qualified call via alias; `as:` alias; multi-alias;
  transitive alias; import with `only:`; local same-module call; alias
  line-visibility; negative: variable-module call emits zero edges.

**PR-1c `feat(elixir-lsp): name/arity identity — QN suffix, pipes, captures, defaults`**
(absorbs D3 from PR-0c)
- **The symmetric arity change, landed atomically (fable analysis):** suffix
  the def QN with `/arity` (`Mod.foo/2`); make the enclosing-caller QN
  module+arity aware so it equals the def QN (parity invariant); compute
  call-site arity on the callee (`arg_count`, +1 for a pipe subject, capture
  `&Mod.fun/2`) so `simple_name`-keyed CALLS resolution matches on both ends.
  All gated on `CBM_LANG_ELIXIR`; do **not** touch `registry.c`/`store.c`/
  `fqn.c`/`graph_buffer.c`/UI. Default args fan out `min..max` (one def per
  arity, same span). Multi-clause heads merge to one def per `name/arity`
  spanning first-to-last clause (module-qualified so cross-module same-named
  helpers don't collide). Rungs (b), (d); `__MODULE__`.
- AC: `probe_elixir_module_calls` stays GREEN (the CALLS canary); pipe
  resolves `fun/2` not `fun/1`; capture `&Mod.fun/2` resolves and emits no
  spurious `fun/0`; `def foo(a, b \\ 1)` resolvable as both arities;
  multi-clause merges to one node with a first-to-last span;
  `test_lang_contract.c` Elixir CALLS stays GREEN.

### Phase 2 — Knowledge and cross-file

**PR-2a `feat(elixir-lsp): curated stdlib seed`** — populate
`elixir_stdlib_data.c` (Kernel/Enum/Map/String/List/Keyword/Process/
GenServer/Supervisor/Task/Agent, name/arity level). AC: `Enum.map/2`,
`GenServer.call/2,3` resolve with `lsp_ex_stdlib` strategy.

**PR-2b `feat(elixir-lsp): cross-file resolution (fallback tier)`** —
`cbm_run_elixir_lsp_cross`, switch cases in `pass_lsp_cross.c:404-421` and
`:547-594`, def filtering by `CBM_LANG_ELIXIR`. AC: `lrp_elixir_s1..s8`
scenarios in `test_lsp_resolution_probe.c`; cross-file call in a two-file
fixture resolves.

**PR-2c `feat(elixir-lsp): use-macro table, behaviours, protocols`** —
curated `use` expansion table; `@behaviour`/`@impl` callback linkage
(callback defs resolve to the behaviour's callback identity); protocol
dispatch: `defimpl` methods registered under `Protocol.For.Target`, calls to
`Protocol.fun(value)` resolve to the protocol def (not a guessed impl). AC:
GenServer `handle_call` links; `use`-injected function resolves; Elixir
cases added to `test_matrix_known_classes.c`.

### Phase 2.5 — Resolver gap closure (pre-promotion)

> Added 2026-07-24 after checkpoint C2. Rationale: C2 measured resolver
> coverage at 44.8 % / 42.4 % / 33.7 % (plug/phoenix/analytics) against a
> 66.7 % ceiling on the clean showcase, and enumerated the textual remainder:
> captures, bare imported calls, stdlib/project name-collisions, and
> arity-mismatched sites. Two of these are **unfinished §3 ladder rungs**
> (`lsp_ex_import` rung (c), `lsp_ex_capture` rung (d) — designed from the
> start, never landed); one is the **outstanding C1 risk-register promise**
> (fuzzy IMPORTS-edge resolution); one is a **correctness defect the C2 rubric
> surfaced** (a stdlib call fabricating a project edge). Close them before
> Phase 3 freezes and documents the shipped behaviour.

**PR-2.5a `feat(elixir-lsp): resolve local captures — ladder rung (d)`**
- Gap: a local capture `&fun/N` has **no `call` node** (it parses as
  `unary_operator(&) → binary_operator(fun / N)`), so `elixir_resolve_calls_in`
  — which only matches `call` nodes — never visits it; the edge forms only via
  the textual pass. (Qualified `&Mod.fun/N` has an inner `call` node and
  already resolves via the arity-aware qualified rung.)
- Fix: match capture-shaped `binary_operator` nodes in the resolution walk via
  the existing `cbm_elixir_capture_arity` helper; an `identifier` left-hand
  side resolves through the local ladder with the literal arity, emitting
  `lsp_ex_capture`.
- AC: the showcase's `&double/1` CALLS edge flips textual → `lsp_ex_capture`;
  unit tests — capture inside `Enum.map`, capture selecting the right arity of
  a multi-arity function, zero-edge for a capture of an unknown function.

**PR-2.5b `feat(elixir-lsp): import-selector resolution — ladder rung (c)`**
- Gap: `import M, only: [f: 1]` followed by a bare `f()` — PASS 1 records the
  imported module but **discards the `only:`/`except:` selectors**, and the
  local rung never consults imports, so bare imported calls stay textual.
- Fix: parse the selector keyword list into `(name, arity)` pairs at PASS 1;
  in the local rung (after file-local and Kernel), consult each import whose
  selector admits `fun/arity` — project modules resolve through the Phase-2b
  cross map (`lsp_ex_import`), curated stdlib modules classify
  (`lsp_ex_stdlib`). A selector that does not admit the call's arity must not
  resolve (no false import edges).
- AC: the showcase's `|> double()` (via `import Showcase.Math, only:
  [double: 1]`, pipe arity 1) resolves `lsp_ex_import` — taking the showcase
  oracle to **6/6 resolver-verified CALLS**; unit tests — `only:` admit,
  `only:` arity-mismatch (no edge), `except:` exclusion.

**PR-2.5c `fix(elixir-lsp): suppress the textual fallback for
resolver-classified external calls`**
- Gap (C2 rubric finding): `Keyword.get(opts, :realm, "…")` is classified
  `lsp_ex_stdlib` (`Keyword.get/3`), but stdlib modules have no graph node, so
  the LSP edge is dropped and the **registry fallback fabricates an edge to
  the project-local `Plug.Router.get/3`** — a false cross-module edge.
- Fix: in `resolve_single_call` (`src/pipeline/pass_calls.c`), Elixir-gated,
  mirroring the existing per-language suppression precedents
  (`cbm_perl_suppress_generic_match`; the TS/JS weak-method suppression that
  drops **only** the plain-CALLS fall-through and preserves service edges):
  when the LSP classified the call as external (`lsp_ex_stdlib` /
  `lsp_ex_kernel` / `lsp_ex_use`) and no target node exists, suppress the
  generic short-name match instead of guessing.
- AC: constructed fixture — a project defining `get/3` plus a call to
  `Keyword.get/3` emits **no** edge to the project `get/3` (fails today); the
  plug rubric spot-check no longer reports the false edge; all CALLS canaries
  stay GREEN. Honest metric note: this removes *false* edges, so the M4 share
  rises partly via a shrinking denominator — C2.5 reports raw counts alongside
  shares.

**PR-2.5d `fix(elixir): resolve IMPORTS edges to the declaring module's Class
node`**
- Gap (the C1 risk-register item, still open): Elixir IMPORTS edges collapse
  onto the app's root module or resolve to nothing —
  `cbm_pipeline_resolve_import_node` (`pass_pkgmap.c`) never matches a dotted
  Elixir module path to its Class node (Strategy 1 composes the wrong QN
  shape; Strategy 2 needs `namespace_name`, unset for Elixir; Strategy 3
  matches name *segments*, never the full dotted name the Class node carries).
- Fix: an Elixir-gated strategy matching `imp->module_path` (the full dotted
  name, post multi-alias expansion) directly against Class-node `name` — the
  same module-identity fact PR-2b's cross map exploits.
- AC: the showcase — whose correctly-extracted in-repo imports currently
  produce **0** resolved IMPORTS edges (the C1 oracle) — gains edges to the
  right Class nodes (e.g. `alias Showcase.Accounts.User` → the `User` Class);
  plug's IMPORTS no longer all collapse onto root `Plug`; IMPORTS counts for
  a non-Elixir control are identical (shared-code change, Elixir-gated).

**Checkpoint C2.5** — re-run the cross-checkpoint matrix (the C2 table gains a
row) on all four codebases: M4 with raw numerator/denominator, the showcase
oracle (target 6/6), IMPORTS-edge accuracy before/after, the TS byte-identical
control, and the false-edge fixture. Recorded in `testing/AFTER-PHASE-2.5.md`;
Phase 3 (PR-3b) then documents these as the final shipped numbers.

### Phase 3 — Evaluation, promotion, documentation

**PR-3a `test(elixir-lsp): QA hardening`** — pathological-input guards
(deep nesting; mirror `perl_lsp.c`'s pre-parse guard), negative-test sweep,
TSan soak, Windows leg. AC: all gates green incl. `make test-tsan`.

**PR-3b `docs(elixir-lsp): promotion`** — README Hybrid LSP table row +
tier re-grade, MAINTAINERS.md release benchmark matrix row
(Elixir → `plausible/analytics`), `docs/BENCHMARK.md` results update, final
§5 after-measurement recorded in `docs/elixir-lsp/testing/`.
- Decision point: Tier-2 prebuilt registry — implement only if §5 M5 shows
  cross-file pass time > ~15 % of total index time on the corpus.

---

## 5. Test & evaluation protocol (empirical, before/after)

Two measurement classes: **objective graph metrics** (M1–M5) computed from
the store, and a **tool-mediated Q&A rubric** (M6) run through a real MCP
client. Both are captured at three checkpoints: **B** (baseline, `main`
before PR-0a), **C1** (after Phase 0), **C2** (after Phase 2). Results live
in this directory:

```
docs/elixir-lsp/testing/
  BASELINE.md        # checkpoint B: metrics + rubric transcript summaries
  AFTER-PHASE-0.md   # checkpoint C1
  AFTER-PHASE-2.md   # checkpoint C2 (promotion evidence for PR-3b)
  questions.md       # the frozen question set + expected-answer key
```

### 5.1 Corpus (pin SHAs in BASELINE.md at first run)

| Repo | Why |
|---|---|
| `plausible/analytics` | Large real app; already in `scripts/clone-bench-repos.sh`; the release-matrix repo |
| `elixir-plug/plug` | Small library; continuity with the historical `docs/BENCHMARK.md` Elixir entry |
| `phoenix-framework/phoenix` | Macro-heavy framework; stresses `use`/router/behaviour handling |

```bash
scripts/clone-bench-repos.sh /tmp/bench          # plausible
git clone --depth 1 https://github.com/elixir-plug/plug /tmp/bench/plug
git clone --depth 1 https://github.com/phoenix-framework/phoenix /tmp/bench/phoenix
```

### 5.2 Build and index each variant in an isolated cache

```bash
# Baseline binary from main; candidate from this branch.
git worktree add /tmp/cbm-main main && (cd /tmp/cbm-main && scripts/build.sh)
scripts/build.sh   # candidate, in the branch checkout

# One cache dir per variant — never share indexes between variants.
CBM_CACHE_DIR=/tmp/cbm-eval/baseline  scripts/benchmark-index.sh /tmp/cbm-main/build/c/codebase-memory-mcp elixir /tmp/bench/analytics /tmp/cbm-eval/results-baseline
CBM_CACHE_DIR=/tmp/cbm-eval/candidate scripts/benchmark-index.sh ./build/c/codebase-memory-mcp        elixir /tmp/bench/analytics /tmp/cbm-eval/results-candidate
# … repeat per corpus repo.
```

### 5.3 Objective metrics (sqlite3 against each variant's store)

Inspect the store schema first (`.tables`, `.schema nodes`) — queries below
are semantic definitions, not copy-paste SQL.

| Metric | Definition | Expected movement |
|---|---|---|
| M1 | Function-def count; separately: count of defs whose source line contains ` when ` (guard heads) | ↑ after PR-0a; guard count > 0 |
| M2 | Share of CALLS edges whose **source node is a Function** (vs Module) | ~0 % → > 90 % after PR-0b |
| M3 | IMPORTS edge count per directive kind | ↑ sharply after PR-0d |
| M4 | Share of CALLS edges carrying an `lsp_ex_*` strategy; breakdown by strategy | 0 % → target ≥ 70 % of qualified+local calls after Phase 2 |
| M5 | Index wall-time and peak RSS from `benchmark-index.sh` output | ≤ +15 % vs baseline (perf guard) |

Also record: total nodes/edges (sanity — large unexplained swings block the
checkpoint), and the unit-suite counts from `scripts/test.sh`.

### 5.4 Rubric evaluation via a real MCP client (M6)

Run the question set through `claude -p` (headless) with the **opus** model,
with each variant's binary mounted as the only MCP server, tools
pre-allowed. Per variant:

```bash
cat > /tmp/cbm-eval/mcp-<variant>.json <<'EOF'
{"mcpServers": {"cbm": {
  "command": "<absolute path to that variant's binary>",
  "env": {"CBM_CACHE_DIR": "/tmp/cbm-eval/<variant>"}
}}}
EOF

claude -p --model opus \
  --mcp-config /tmp/cbm-eval/mcp-<variant>.json \
  --allowedTools "mcp__cbm__*" \
  "<question text>  Answer using only the MCP graph tools. Cite the tool calls you made."
```

**Question set** (frozen in `testing/questions.md` before checkpoint B; 12
general questions per `docs/BENCHMARK.md` methodology + 5 Elixir-specific):

- The 12 standard rubric questions (schema, find-function, snippet, text
  search, trace, cypher, structure — as defined in `docs/BENCHMARK.md`).
- E1: "Which functions does `Plug.Conn.put_resp_header/3` call, and who
  calls it?" (arity-correct trace)
- E2: "Trace the pipeline `conn |> put_status(404) |> halt()` — which
  modules do these resolve to?" (pipe + import resolution)
- E3: "List the GenServer callbacks implemented in <module from corpus>."
  (use/behaviour linkage)
- E4: "Which modules implement protocol `Phoenix.HTML.Safe`?" (defimpl)
- E5: "Where is the route for `POST /api/event` handled?" (Phoenix router —
  should already partially work; regression guard)

**Protocol discipline:**

- Up to 3 attempts per question (fresh `claude -p` invocation each); best
  answer counts — matching `docs/BENCHMARK.md`'s attempt allowance.
- Grade PASS (1.0) / PARTIAL (0.5) / FAIL (0.0) against the expected-answer
  key in `questions.md`. Grading is done blind: the grader (a separate
  `claude -p` invocation given only question, key, and answer — or a human)
  must not know which variant produced the answer.
- Record per question: grade, attempt count, tool calls used, and one-line
  failure reason. Summarise per checkpoint file; commit summaries (not raw
  transcripts) to `testing/`.
- The same person/agent-version runs both variants at a checkpoint on the
  same day; model is pinned (`--model opus`; record the full model ID that
  the CLI reports).

### 5.5 Regression safety net (non-Elixir)

At C1 and C2, re-run `scripts/benchmark-index.sh` node/edge counts for one
non-Elixir control repo (e.g. the Go or Python bench repo) on both variants:
counts must be identical between variants (Elixir work must not perturb
other languages). `scripts/test.sh` full suite is the per-PR guard.

---

## 6. Progress tracker

> Update this table in the same PR as the work it describes. Add a dated
> line to the log below when a checkpoint measurement is recorded.

| Item | PR | Status |
|---|---|---|
| Tracking issue opened, linked here | [#1](https://github.com/holsee/codebase-memory-mcp/issues/1) | ☑ 2026-07-23 |
| Checkpoint B recorded (`testing/BASELINE.md`, `questions.md` frozen) | — | ☑ 2026-07-23 |
| PR-0a guard-clause + def-like forms | `e18cb5c1` | ☑ 2026-07-23 |
| PR-0b enclosing-function attribution | `d987aeca` | ☑ 2026-07-23 |
| PR-0c nested module QNs (D6); D3 → PR-1c | `27af2583` | ☑ 2026-07-23 |
| PR-0d module-body directives | `214b2fe6` | ☑ 2026-07-23 |
| PR-0e vars + contract strength | `0a25d2f4` | ☑ 2026-07-23 |
| Checkpoint C1 recorded (`testing/AFTER-PHASE-0.md`) | — | ☑ 2026-07-24 |
| PR-1a skeleton + wiring + originality rows | `15985052` | ☑ 2026-07-24 |
| PR-1b scopes/aliases/imports resolution | `d7da0ca7` | ☑ 2026-07-24 |
| PR-1c name/arity identity (D3) + pipes/captures/defaults | `79ebbc7d` | ☑ 2026-07-24 |
| Checkpoint C1.5 recorded (`testing/AFTER-PHASE-1.md`) | — | ☑ 2026-07-24 |
| PR-2a stdlib seed | `dde6f85f` | ☑ 2026-07-24 |
| PR-2b cross-file fallback tier | `9dfb2142` | ☑ 2026-07-24 |
| PR-2c use-table + behaviours + protocols | `c3071bd6` | ☑ 2026-07-24 |
| Checkpoint C2 recorded (`testing/AFTER-PHASE-2.md`) | — | ☑ 2026-07-24 |
| PR-2.5a local captures (`lsp_ex_capture`, ladder rung d) | `69c058a8` | ☑ 2026-07-24 |
| PR-2.5b import-selector resolution (`lsp_ex_import`, ladder rung c) | | ☐ |
| PR-2.5c external-call textual-fallback suppression | | ☐ |
| PR-2.5d IMPORTS-edge resolution to Class nodes (C1 register item) | | ☐ |
| Checkpoint C2.5 recorded (`testing/AFTER-PHASE-2.5.md`) | — | ☐ |
| PR-3a QA hardening | | ☐ |
| PR-3b docs + promotion + release matrix | | ☐ |

**Log**

- 2026-07-23 — Plan authored on `feat/elixir-hybrid-lsp`. Research basis:
  repo audit (defects D1–D8), integration-contract map, Expert/tags.scm
  design study. Expert cloned at `~/workspace/_oss/expert` for reference.
- 2026-07-23 — Dev container added (`.devcontainer/`) mirroring the CI
  toolchain; includes the evaluation-protocol tools (sqlite3, Claude CLI).
- 2026-07-23 — Moved to fork `holsee/codebase-memory-mcp` (upstream issue
  DeusData#1239 closed in favour of fork issue #1).
- 2026-07-23 — Checkpoint B recorded (`testing/BASELINE.md`): guard-head
  defs 0 % everywhere (D1 confirmed); Function-sourced CALLS 56–70 %
  (plan's ~0 % prediction was too pessimistic — target > 90 % stands);
  zero Elixir `lsp_*` strategies. Rubric 17/17 PASS **with instrument
  critique**: agentic opus routes around graph gaps (E1 answered via
  7-step text fallback because `put_resp_header` had no node) — C1/C2
  runs must use `--disallowedTools "Bash,Read,Grep,Glob"` and grade
  multi-conjunct keys strictly; tool-path shrinkage is part of the claim.
- 2026-07-23 — PR-0a landed (`e18cb5c1`). Plug spot-check: guarded
  functions with a node 58/82 → **82/82**; nodes 989 → 1,030.
  Deviation: defstruct/defexception node emission deferred to Phase 2
  (data, not callables). Known local-env caveat: pre-existing
  `src/ui/httpd.h` clang-tidy finding blocks the full pre-commit hook on
  this machine; cppcheck + clang-format legs verified green directly.
- 2026-07-23 — PR-0b landed. **D2 framing corrected by measurement:**
  plain-`def` enclosing-function attribution was *already* GREEN on main
  (compute_elixir_func_qn already gated def/defp/defmacro); the
  `repro_grammar_functional_elixir` "dim 7 RED" comment was stale (the
  non-guarded fixture passed). The real gap was (a) **guarded** heads
  (`def f(x) when …`, a binary_operator first-arg) and (b) the def-like
  forms PR-0a added (defmacrop/defguard(p)/defn(p)/defdelegate) — none
  recognised by compute_elixir_func_qn, so their bodies' calls sourced to
  the module. Fix: shared `cbm_elixir_def_macro()` keyword predicate +
  guarded-head descent in both `cbm_find_enclosing_func` (now takes
  `source` to verify the def target) and `compute_elixir_func_qn`. The
  repro fixture is now guarded so it is a true before/after guard: FAILS
  dim 7 on main ("1 in-body CALLS sourced at Module"), PASSES on PR-0b.
  Full suite 6780/0; repro battery 318/6 identical to main (6 pre-existing
  env/known-red failures, zero regressions). Note: whole-file clang-format
  reflow of the repro file was reverted — test/repro files are not in the
  `lint-format` gate (LINT_SRCS), so the PR keeps a minimal diff.
- 2026-07-23 — PR-0c **re-scoped after a fable consumer-impact analysis** of
  the arity-in-QN question. Verdict: arity identity (`Mod.foo/2`) is correct
  and the store supports it, but the CALLS resolver keys on
  `simple_name(qualified_name)` at **both** the def and call-site ends
  (`registry.c`), so a def-side-only arity suffix breaks every intra-module
  Elixir CALLS edge — the def QN, enclosing-caller QN, and callee arity must
  land together. That callee work is PR-1c, so **D3 moved to PR-1c** to land
  atomically; PR-0c ships only D6 (nested-module QNs), which is independent
  and safe. Empirical baseline for D3 (for PR-1c): multi-clause redundancy
  44 % on plug / 19 % on analytics; `UNIQUE(project, qualified_name)` already
  collapses clauses to one node, so foo/1 and foo/2 *wrongly* merge today —
  that is the identity gap PR-1c closes. D6 shipped: `elixir_nested_module`
  fails on main (`Foo.Bar` absent), passes here; CALLS canary
  `probe_elixir_module_calls` and the 53-language CALLS-breadth probe stay
  GREEN (Class-QN-only change, no resolver path touched).
- 2026-07-23 — PR-0d landed (D5). Dedicated `parse_elixir_imports` recursively
  descends `defmodule` do-blocks matching the four directives
  (import/alias/require/use), handling `as:` aliases and `Foo.{Bar, Baz}`
  multi-alias; replaces the root-only `parse_generic_imports("call")` that both
  missed nested directives and mis-read the root `defmodule` as an import.
  Test fixture rewritten to the idiomatic nested form (fails on main, passes
  here). Plug IMPORTS(ex) **40 → 92**. Scope note: directive-kind (which of the
  four) is *not* recorded in edge metadata — `CBMImport` has no kind field and
  adding one ripples through the shared all-language edge-emission pipeline;
  deferred (the four are semantically distinct for the resolver but all map to
  an IMPORTS edge for the graph). Full suite 6781/0; extraction + lang_contract
  + grammar_imports green.
- 2026-07-23 — PR-0e landed (D7, D8). **D7 framing corrected by measurement:**
  the audit said the `{"binary_operator"}` var spec produced spurious vars from
  every expression, but the variable walk only reaches module/top-level nodes
  (never function bodies), so nested assignments aren't extracted at all — the
  real spurious-var risk is top-level bare expressions (`a + b`) whose first
  operand the default fallback mints as a Variable. Fix: an Elixir case in
  `extract_var_names` that binds only on the `=` match operator (verified via
  the `.operator` field — AST-probed to confirm) with an identifier LHS.
  `elixir_variable_binding` fails on main (spurious operand bound), passes here.
  D8: the Elixir regression contract was the suite's weakest (min_defs=1, no
  names); strengthened to `2, {foo, A}` — name-verified, at parity with Ruby.
  Scope note: in-function-body variable extraction remains out of scope for the
  grammar layer (the walk is module-level by design); not needed for call
  resolution. **Phase 0 (D1–D8) complete** — next is checkpoint C1.
- 2026-07-24 — Checkpoint C1 recorded (`testing/AFTER-PHASE-0.md`).
  Objective (B → C1): Function-sourced CALLS 56→68 % / 70→76 % / 60→63 %;
  Functions +37/+94/+107; index time flat. Rubric 17/17 PASS under the stricter
  MCP-only runner (baseline reached 17/17 partly via text fallback the C1 runner
  forbids) — graph-gap signals 3→1 questions; E1 went from a 7-tool `grep`
  cascade to 4 clean graph tools. Non-Elixir control (graph-ui TS, 46 files):
  baseline and C1 binaries produce byte-identical graphs (338 nodes/764 edges) —
  Phase 0 confirmed Elixir-scoped. **Correction:** the IMPORTS *edge* count
  (40→92 etc.) was demoted from headline — the pinned corpus showed Elixir
  import→node resolution is fuzzy (all plug IMPORTS collapse to the root `Plug`
  module; the showcase resolves 0 from correct in-repo imports). PR-0d's real
  win is import *extraction* (unit-tested); accurate cross-module resolution is
  a newly-tracked Phase 2 target.
- 2026-07-24 — PR-1a landed (`15985052`). Elixir Hybrid LSP resolver **skeleton**
  + wiring: `elixir_lsp.{c,h}` (ElixirLSPContext mirroring PerlLSPContext;
  init/entry lifecycle + a depth-guarded **no-op** walk), `generated/
  elixir_stdlib_data.c` (Kernel auto-import stub), `tests/test_elixir_lsp.c`
  scaffold. Wired at the per-file dispatch (`cbm.c`), unity build (`lsp_all.c`),
  the five test touchpoints, and `check-lsp-originality.sh` REFS[] (scanning
  against Expert, Apache-2.0). The walk emits nothing, so the graph is
  **byte-identical** to the Phase-0 tip — verified on `elixir_showcase`
  (Functions=18, Classes=8, 8/8 capability checks unchanged). Gates: prod +
  ASan/UBSan build clean (-Werror); full suite **6621 passed / 0 failed / 4
  skipped**; clang-format clean; originality scan **CLEAN** (no verbatim/
  structural overlap). Deviation: `lrp_elixir_s1..s8` probe scenarios moved to
  PR-1b (a coarse CALLS-floor matrix carries no signal against a no-op walk —
  calibrate them against real resolution). Delivery: three **stacked** PRs
  (PR-1a→1b→1c) on the fork; a full interim checkpoint (`AFTER-PHASE-1.md`,
  M1–M5 + rubric) is recorded at the tip of the stack.
- 2026-07-24 — PR-1b landed (`d7da0ca7`). Resolution core: two-pass walk
  (PASS 1 collects file-defined modules + line-ordered alias/import/use
  directives incl. multi-alias `Foo.{Bar,Baz}`, `as:`, transitive chaining;
  PASS 2 tracks the enclosing def QN = `module_qn.name` — matching the
  extractor's path-based QN so CALLS edges source correctly — and resolves
  local (`lsp_ex_local`) and same-file-qualified (`lsp_ex_qualified`) calls).
  **Key architectural finding:** Elixir def QNs are path-based
  (`<module_qn>.<name>`, module name not woven in), so a def resolves via
  `cbm_registry_lookup_symbol(module_qn, name)` whether the call was bare or
  qualified against a *same-file* module. **Cross-file / cross-module
  resolution is therefore Phase 2b** (mapping a dotted module name to another
  file's defs needs the project-wide registry) — PR-1b keeps the zero-edge
  guarantee for external / variable-module / `apply` dispatch. Graph-level M4
  signal confirmed on `elixir_showcase`: a same-module CALLS edge now carries
  `strategy=lsp_ex_local` (0 → present); cross-file `User.new`/`Math.double`
  and the `&double/1` capture correctly stay textual (Phase 2b / Phase 1c).
  10 `test_elixir_lsp` cases + 8 `lrp_elixir_s1..s8` probe scenarios (S3 the
  documented cross-file RED). Full suite 6637/0. Note: the textual resolver
  already resolves much of this corpus by simple-name, so PR-1b's single-file
  value is strategy-tagging (M4), alias-gated precision, and zero-edge
  discipline; the ≥70 % M4 target is Phase 2.
- 2026-07-24 — PR-1c landed (`79ebbc7d`). **D3 name/arity identity** (deferred
  here from PR-0c). Def QNs become `Mod.foo/2`; foo/1 and foo/2 are distinct
  nodes. Landed as the **symmetric** change the PR-0c fable analysis required —
  `/arity` appended byte-identically at (1) the def node QN (`extract_defs.c`),
  (2) the enclosing/caller scope QN (`extract_unified.c`), (3) the call-site
  `callee_name` (`extract_calls.c`), and (4) the LSP resolver's caller QN +
  callee lookups (`elixir_lsp.c`) — all `CBM_LANG_ELIXIR`-gated; pipeline/store/
  schema/fqn/graph_buffer untouched (QN opaque; `simple_name` splits on `.`/`::`
  so `/N` survives). Three shared AST helpers in `helpers.c`
  (`cbm_elixir_def_arity`/`_call_arity`/`_capture_arity`) — **`CBMCall.arg_count`
  is never populated for Elixir**, so arity is read straight from the AST.
  Default args fan out one node per arity `min..max`; pipe `x |> f(y)` = `f/2`;
  capture `&f/N` uses the literal N. **Design delegated to fable** (per standing
  instruction), which verified against the grammar + live resolver and corrected
  three wrong assumptions (arg_count=0; caller QN spans 3 sites incl. the LSP;
  captures resolve via the generic path so naive arity breaks `&fun/N`). Only one
  test assertion needed updating (`test_extraction.c` control-flow →
  `handle/1`); `def.name` stays bare so `has_def`/`search_graph name_pattern`
  are unaffected. Graph proof on `elixir_showcase`: `user.promote/1` and
  `user.promote/2` are now distinct nodes; `handle_call→tick/2` resolves
  `lsp_ex_local`; `&double/1`→`double/1`. 3 new arity tests; canaries
  (`probe_elixir_module_calls`, 53-lang CALLS-breadth, lang_contract, grammar,
  lrp_elixir S4 pipe/S5 capture) all GREEN. Full suite 6640/0. **Phase 1
  complete** — next is the checkpoint C1.5 (`AFTER-PHASE-1.md`).
- 2026-07-24 — Checkpoint C1.5 recorded (`testing/AFTER-PHASE-1.md`),
  Phase-1 tip (`79ebbc7d`) vs Phase-0 tip (C1) to isolate the resolver.
  **M4 (lsp_ex_* CALLS share) 0 → 34.9 % (plug) / 32.5 % (phoenix) / 18.3 %
  (analytics)** — the first non-zero M4 for Elixir (≥70 % is the Phase-2
  cross-file target; analytics is lower as a large app makes more cross-module
  calls). M1: 100 % of functions now arity-identified; `foo/1` and `foo/2` are
  distinct nodes. **The CALLS-total drop is a precision gain, not a regression:**
  intra-file CALLS preserved (plug 583→573, phx 1349→1324, an 3425→3234) while
  cross-file dropped ~32–37 % — arity correctly rejects fuzzy cross-module
  by-name matches (e.g. `Enum.map`→a project `map`); accurate cross-module
  resolution returns arity-precise in Phase 2b. Index cost flat (≤ +12 %).
  Rubric (M6, focused): the arity-disambiguation question flips **FAIL → PASS**
  (baseline: "arity not represented, one node for get_session"; candidate:
  `read_body/1` vs `/2`, `send_file/5`, cross-module `__catch__/5` vs `/6`), and
  the E1 arity-trace goes from an 18-`get_code_snippet` reconstruction + a false
  edge to a 4-call `trace_path` with native arity. Non-Elixir control (graph-ui
  TS, 45 files): Phase-0-tip and Phase-1 binaries produce **byte-identical**
  graphs (338 nodes / 764 edges) — Phase 1 confirmed Elixir-scoped.
- 2026-07-24 — PR-2a landed (`dde6f85f`). Curated stdlib seed:
  `elixir_stdlib_data.c` expanded to ~150 arity-keyed entries (Kernel +
  Enum/Map/String/List/Keyword/Process/GenServer/Supervisor/Task/Agent), plus
  two resolver rungs — a Kernel fallback for bare calls (`lsp_ex_kernel`) and a
  curated-module rung for qualified calls (`lsp_ex_stdlib`), both arity-precise
  (`GenServer.call/2` vs `/3`). **Scope decision:** the seed is *knowledge*, not
  graph nodes — a resolved stdlib call carries the strategy in `resolved_calls`
  but forms no CALLS *edge* (stdlib modules aren't indexed; edges need an
  existing node). Minting stdlib nodes (à la `kotlin_builtins.c`) would inflate
  every project's node count and break the label goldens, so it's deliberately
  deferred/opt-in; the real Phase-2 graph win is cross-file resolution to
  existing project nodes (PR-2b). Matches the Perl precedent (stdlib classifies,
  doesn't create edges). 3 new tests; full suite 6643/0.
- 2026-07-24 — PR-2b landed (`9dfb2142`). **Cross-file resolution** — a
  `Mod.fun/arity` call to a module in another file now resolves to that module's
  def node with the `lsp_ex_cross` strategy. **Module-identity crux solved
  (fable-designed):** Elixir def QNs are path-based and `CBMLSPDef` carries no
  Elixir module name, so `cbm_run_elixir_lsp_cross` recovers
  `{ElixirModuleName → def_module_qn}` from the Class defs each `defmodule`
  already emits, registers Function defs under their real QNs, and resolves via
  alias-expand → map → `lookup_symbol(def_module_qn, "fun/arity")`. Wired in
  `pass_lsp_cross.c` (include + `has_cross_lsp` + `run_one` switch) mirroring
  Kotlin, **plus a filter exemption** (join Rust) so the whole project's defs
  reach the resolver — the default own+imported-module filter starves Elixir
  (fully-qualified calls have no import; IMPORTS QNs point at Class nodes). Safe
  because Elixir module names are globally unique. The shared per-file walk is
  reused via a `cross_module_map`-gated branch (NULL per-file → Phase 1
  byte-preserved). `import`/alias arrays accepted but not fed (aliases from
  source; fuzzy IMPORTS QNs would mis-target — `only:`/`except:` selector
  resolution stays Phase 2). fable corrected the brief on two points (disable the
  module filter; recover identity from Class defs rather than populate
  `namespace_name`). 3 direct `cbm_run_elixir_lsp_cross` unit tests + S3 probe
  flipped GREEN. **End-to-end proof (showcase):** `User.new/2`/`User.promote/1`
  cross-file calls in accounts.ex now resolve `lsp_ex_cross` to the user.ex def
  nodes, where C1.5 had them as fuzzy `unique_name` — this is the fuzzy-import
  finding closed for qualified calls. Full suite 6646/0.
- 2026-07-24 — PR-2c landed (`c3071bd6`). **use-macro table + protocol dispatch.**
  A `use Framework` injected-function table (Phoenix.Controller/LiveView/Component,
  Ecto.Schema, ExUnit.Case, GenServer/Supervisor child_spec) + a local-rung pass:
  a bare call inside a def that is not file-local/Kernel is resolved against the
  functions the module's `use`d frameworks inject (`lsp_ex_use`). PASS 1 now
  collects `use` targets into `ctx->use_module`. Protocol dispatch
  (`Protocol.fun(x)` → the protocol's own def) already flows through the PR-2b
  cross map (defprotocol emits a Class + its def functions); a unit test pins it.
  **Scope/honesty:** module-level use-macros (Ecto `field`, ExUnit `test`) have
  no enclosing function → no caller QN → no edge (correct: a module-level macro
  is not a fn-to-fn call); only use-injected functions called *inside a def*
  (Phoenix.Controller.render in an action) resolve. `@behaviour`/`@impl`
  OVERRIDE-edge linkage deferred (needs behaviour-callback nodes — the PR-2a
  node-injection tradeoff); the use-table carries the callback knowledge.
  Elixir cases NOT added to `test_matrix_known_classes.c` (reference-only suite,
  not run in test_main.c — executable coverage is in `test_elixir_lsp.c`, now 21
  cases). 2 new tests; full suite 6648/0. **Phase 2 complete** — next is the C2
  checkpoint.
- 2026-07-24 — Checkpoint C2 recorded (`testing/AFTER-PHASE-2.md`), Phase-2 tip
  (`c3071bd6`) vs Phase-1 tip (C1.5). **M4 (lsp_ex_* CALLS share) +10–15 pts:
  plug 34.9→44.8 %, phoenix 32.5→42.4 %, analytics 18.3→33.7 %**, driven by
  cross-file `lsp_ex_cross` edges (plug breakdown: local 412 / cross 121 /
  qualified 5 LSP vs unique_name 392 / same_module 151 / suffix_match 121
  textual). Index cost flat; M1 arity 100 %; node counts unchanged (stdlib is
  knowledge, not nodes). **≥70 % target NOT reached at the edge level (honest):**
  the textual remainder is captures (`&f/N`), bare imported calls (`import
  only:`), arity-mismatched sites, and stdlib/project name-collisions — the C2
  rubric surfaced a concrete one (`Keyword.get/3` mis-resolved by the *textual*
  pass onto project `Plug.Router.get/3`; the stdlib rung classifies it but
  can't suppress the textual edge without a node/suppression signal). All
  enumerated as Phase-3 refinements. Cross-file end-to-end proof (showcase
  `User.new/2`/`User.promote/1` → `lsp_ex_cross`). Non-Elixir control
  byte-identical (graph-ui TS 338/764). Rubric (focused, P2): cross-module trace
  + arity work confirmed; model independently flagged the stdlib collision.
- 2026-07-24 — **Phase 2.5 (resolver gap closure) planned and inserted before
  Phase 3**, from the C2 gap enumeration. Four PRs: 2.5a local captures
  (`lsp_ex_capture` — §3 ladder rung (d), designed from the start but never
  landed: local `&fun/N` has no `call` node so the resolver walk skips it);
  2.5b import-selector resolution (`lsp_ex_import` — ladder rung (c): selectors
  are currently parsed and discarded); 2.5c external-call textual-fallback
  suppression (the C2 rubric's `Keyword.get/3` → project `Plug.Router.get/3`
  false edge; mechanism verified — mirrors `cbm_perl_suppress_generic_match` /
  the TS-JS plain-CALLS-only suppression in `pass_calls.c`); 2.5d IMPORTS-edge
  resolution to Class nodes (the outstanding C1 risk-register item — PR-2b
  closed its CALLS half only). Checkpoint C2.5 re-runs the cross-checkpoint
  matrix with raw counts alongside shares (2.5c shrinks the denominator).
  Framing: not new scope — two unfinished ladder rungs, one open register
  promise, one rubric-surfaced correctness defect, closed before Phase 3
  freezes and documents shipped behaviour.
- 2026-07-24 — PR-2.5a landed (`69c058a8`). Local captures resolve:
  capture-shaped `binary_operator` nodes (`&fun/N` has no `call` node) are now
  matched in the resolution walk — identifier LHS, integer RHS, node IS the `/`
  operator (guarding against the left-of-`/` double-match) — and resolve
  file-local at the literal arity (`lsp_ex_capture` @ 0.90; captured Kernel
  builtins classify `lsp_ex_kernel`). Showcase oracle: the `&double/1` edge
  flipped `same_module` → `lsp_ex_capture` (**5/6** resolver-verified; the
  remaining textual edge is 2.5b's imported bare call). 3 new tests (capture in
  Enum.map, arity selection f/2-not-f/1, unknown-fn zero-edge). Full suite
  6651/0.
  with no test exercising them: PR-0d's multi-alias `Foo.{Bar, Baz}` expansion
  and `alias X, as: Y` handling (the grammar_imports fixture used only plain
  directives), and PR-0c's *nested* `defimpl` prefix. Added
  `elixir_alias_forms` and `elixir_nested_defimpl` (both fail on main, pass
  here) plus `elixir_call_under_control_flow` — a stays-green guard that a call
  inside an `if`/`case` macro still attributes to the enclosing def (PR-0b).
  The corpus-impact audit that prompted this: PR-0e's D7 var-guard and PR-0c's
  nesting showed near-zero movement on the three repos because their triggering
  patterns are rare/absent there — real defects, but the unit tests (not the
  corpus numbers) are their evidence, so the untested string-parsing paths were
  the genuine risk.

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Macro opacity (`use`, metaprogramming) caps resolution | Curated table for the dominant frameworks; zero-edge rule for the rest — precision over recall, matching Perl's shipped stance |
| D3 arity redesign ripples into QN format consumers (store, search, UI) | Decide arity representation in PR-0c against actual consumers; keep QN string format unchanged if `name/arity` can live in a def field instead |
| Corpus rubric is partly subjective | Blind grading, frozen question key, 3-attempt cap, objective M1–M5 carry the primary claim |
| Index-time regression from cross-file pass | M5 gate ≤ +15 %; Tier-2 registry as the escape hatch |
| Originality scan flags resolver code | Clean-room discipline: design-notes-first, no source open while writing; scan locally before each resolver PR |
| **Fuzzy Elixir import→node resolution** (surfaced by the corpus at C1): dotted module imports collapse to the app root module or resolve to nothing — `cbm_pipeline_resolve_import_node` doesn't map `Mod.Sub` to its declaring module node | **PR-2.5d** (Phase 2.5): an Elixir-gated strategy matching the full dotted `module_path` against Class-node names — the same module-identity fact the PR-2b cross map exploits; the `elixir_showcase` corpus (0 IMPORTS edges from correct in-repo imports) is the acceptance oracle. Phase 2 (PR-2b) closed the *CALLS* half of this finding; the IMPORTS-edge half is 2.5d |
