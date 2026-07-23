# Elixir Hybrid LSP — Execution Plan

**Status:** In progress · **Branch:** `feat/elixir-hybrid-lsp` (fork `holsee/codebase-memory-mcp`) · **Tracking issue:** [holsee/codebase-memory-mcp#1](https://github.com/holsee/codebase-memory-mcp/issues/1)
**Last updated:** 2026-07-23

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

**PR-0c `fix(elixir): name/arity identity and nested module QNs`** (D3, D6)
- Compute arity (count params; `\\` defaults fan out `min..max` — one def
  per arity, or one def annotated with arity range if def dedup is
  preferred; decide in-PR, document in the file header). Join nested
  `defmodule` QNs with the parent path. Dedupe multi-clause heads to one
  def per `name/arity` spanning first-to-last clause lines.
- AC: extraction tests for multi-clause, default-args, nested modules.

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

**PR-1c `feat(elixir-lsp): arity semantics — pipes, captures, defaults`**
- Rungs (b), (d); default-arg fan-out in the registry; `__MODULE__` and
  multi-clause caller attribution polish.
- AC: pipe resolves `fun/2` not `fun/1`; capture `&Mod.fun/2` resolves and
  emits no spurious `fun/0`; `def foo(a, b \\ 1)` resolvable as both
  arities.

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
| PR-0b enclosing-function attribution | `feat/elixir-hybrid-lsp` | ☑ 2026-07-23 |
| PR-0c name/arity + nested QNs | | ☐ |
| PR-0d module-body directives | | ☐ |
| PR-0e vars + contract strength | | ☐ |
| Checkpoint C1 recorded (`testing/AFTER-PHASE-0.md`) | — | ☐ |
| PR-1a skeleton + wiring + originality rows | | ☐ |
| PR-1b scopes/aliases/imports resolution | | ☐ |
| PR-1c pipes/captures/default arities | | ☐ |
| PR-2a stdlib seed | | ☐ |
| PR-2b cross-file fallback tier | | ☐ |
| PR-2c use-table + behaviours + protocols | | ☐ |
| Checkpoint C2 recorded (`testing/AFTER-PHASE-2.md`) | — | ☐ |
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

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Macro opacity (`use`, metaprogramming) caps resolution | Curated table for the dominant frameworks; zero-edge rule for the rest — precision over recall, matching Perl's shipped stance |
| D3 arity redesign ripples into QN format consumers (store, search, UI) | Decide arity representation in PR-0c against actual consumers; keep QN string format unchanged if `name/arity` can live in a def field instead |
| Corpus rubric is partly subjective | Blind grading, frozen question key, 3-attempt cap, objective M1–M5 carry the primary claim |
| Index-time regression from cross-file pass | M5 gate ≤ +15 %; Tier-2 registry as the escape hatch |
| Originality scan flags resolver code | Clean-room discipline: design-notes-first, no source open while writing; scan locally before each resolver PR |
