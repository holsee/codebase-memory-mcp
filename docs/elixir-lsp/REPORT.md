# Elixir support in codebase-memory-mcp — capability report

**Date:** 2026-07-24 · **State assessed:** `feat/elixir-lsp-p25d` (Phase 0 → 2.5
complete; Phase 3 hardening/promotion pending) · **Evidence:**
`testing/BASELINE.md`, `AFTER-PHASE-0/1/2/2.5.md`, the pinned
`testing/corpus/elixir_showcase` oracle, and the unit/probe suites.

This report answers three questions: *how* an Elixir project is processed,
*what* is and is not supported (with the limitations that remain), and whether
the work delivers **tangible** improvement for someone using the MCP tools on
an Elixir codebase.

---

## 1. How an Elixir project is processed (the full map)

An `.ex`/`.exs` file flows through six stages. Stages 2–6 all contain
Elixir-specific work added by this effort; stage 1 predates it.

```
 discovery ─▶ grammar extraction ─▶ per-file resolver ─▶ cross-file resolver
                                                              │
              store / graph  ◀─ pipeline edge resolution  ◀───┘
                    │
              MCP tools (search_graph, trace_path, query_graph, …)
```

**Stage 1 — discovery.** `src/discover/language.c` maps `.ex`/`.exs` to
`CBM_LANG_ELIXIR`; the vendored tree-sitter-elixir grammar parses the file.
(Pre-existing.)

**Stage 2 — grammar extraction** (`internal/cbm/extract_*.c`, Phase 0 + 1c).
tree-sitter-elixir represents `def`, `defmodule`, `import` — and every
ordinary call — as plain `call` nodes, so everything below rests on
target-keyword recognition (the tags.scm model):

- *Definitions* (`extract_defs.c`): `def`/`defp`/`defmacro(p)`/`defguard(p)`/
  `defn(p)`/`defdelegate` become Function nodes — **including guarded heads**
  (`def f(x) when …`, D1) — with QN `<path-module>.<name>/<arity>` (D3):
  `foo/1` and `foo/2` are distinct nodes, and default args fan out
  (`def f(a, b \\ 1)` defines both `f/1` and `f/2`). `defmodule`/`defprotocol`
  become Class nodes named with the **full dotted module name**, nesting
  joined (`Outer.Inner`, D6); `defimpl P, for: T` becomes a `P.T` Class (D4).
- *Calls* (`extract_calls.c`, `extract_unified.c`): every call site records an
  arity-suffixed callee (`Mod.fun/2`) — pipes add the piped argument
  (`x |> f(y)` → `f/2`), captures carry their literal arity (`&f/1`) — and is
  attributed to its **enclosing function** (guards included, D2), not the
  module.
- *Directives* (`extract_imports.c`): `alias`/`import`/`require`/`use` are
  scanned **inside** `defmodule` bodies (D5), including multi-alias
  `Foo.{Bar, Baz}` and `as:` renames.
- *Framework patterns* (`extract_channels.c`, `service_patterns.c`,
  pre-existing): Phoenix channels/PubSub and Oban/Broadway service edges.

**Stage 3 — per-file semantic resolver**
(`internal/cbm/lsp/elixir_lsp.c`, Phases 1–2.5). A two-pass AST walk per file.
PASS 1 collects the file's modules and its line-ordered directives (aliases
with transitive chaining and line visibility; imports with parsed
`only:`/`except:` selectors; `use` targets). PASS 2 tracks the enclosing def
and resolves each call down a ladder — each rung emits a
`{caller, callee, strategy, confidence}` record, or **nothing** (the zero-edge
guarantee: a false edge is worse than a missing one):

| Call shape | Resolves to | Strategy (confidence) |
|---|---|---|
| bare `fun(args)` | file-local def at that arity | `lsp_ex_local` (0.90) |
| bare, selector-admitted import | the imported module's def | `lsp_ex_import` (0.85) |
| bare, Kernel builtin | curated Kernel entry | `lsp_ex_kernel` (0.85) |
| bare, `use`-injected (render, assign, …) | curated framework entry | `lsp_ex_use` (0.85) |
| capture `&fun/N` | file-local def at literal N | `lsp_ex_capture` (0.90) |
| `Mod.fun(a)`, Mod in this file / `__MODULE__` | file-local def | `lsp_ex_qualified` (0.90) |
| `Mod.fun(a)`, Mod in another file | that module's def (cross pass) | `lsp_ex_cross` (0.85) |
| `Mod.fun(a)`, curated stdlib (Enum, Map, GenServer…) | curated entry | `lsp_ex_stdlib` (0.85) |
| variable module, `apply/3`, `unquote`, unknown | **no edge** | — |

**Stage 4 — cross-file resolver** (`cbm_run_elixir_lsp_cross`, Phase 2b). The
pipeline hands the resolver every project definition (Elixir is
filter-exempt, like Rust, because its common idiom is a fully-qualified call
with no import). The crux — Elixir def QNs are path-based, not
module-name-based — is solved by recovering a `{module name → path prefix}`
map from the Class nodes every `defmodule` already emits, then looking up
`prefix.fun/arity`. The same map powers stage 6's IMPORTS resolution.

**Stage 5 — pipeline edge resolution** (`src/pipeline/pass_calls.c` /
`pass_parallel.c` / `lsp_resolve.h`). Resolver records override the textual
name-matcher when the callee is a real graph node; where the resolver
classified a call as **known-external** (stdlib/Kernel/use) and no node
exists, the textual fallback is **suppressed** (Phase 2.5c) so it cannot
fabricate an edge to a same-named project function. Calls the resolver never
touched still resolve textually (`unique_name`/`same_module`/`suffix_match`).

**Stage 6 — IMPORTS-edge resolution** (`pass_pkgmap.c`, Phase 2.5d). An Elixir
directive's dotted module name is matched **exactly** against Class-node
names; a hit becomes an IMPORTS edge to the true declaring module, a miss
(stdlib, deps) forms no edge. The fuzzy fall-through — which had collapsed
every plug import onto the bare root `Plug` node — is disabled for Elixir.

Everything above is `CBM_LANG_ELIXIR`- or `.ex`-gated; a TypeScript control
tree indexes **byte-identically** under every checkpoint pair.

---

## 2. What the work delivered (measured)

Resolver-verified share of Elixir call edges (M4) — raw counts in the
checkpoint files:

| | plug | phoenix | analytics | showcase (oracle) |
|---|---|---|---|---|
| Baseline / Phase 0 | 0 % | 0 % | 0 % | 0 % |
| Phase 1 (per-file + arity) | 34.9 % | 32.5 % | 18.3 % | 16.7 % |
| Phase 2 (+ cross-file) | 44.8 % | 42.4 % | 33.7 % | 66.7 % |
| **Phase 2.5 (+ gap closure)** | **54.3 %** | **48.4 %** | **36.9 %** | **100 %** |

And the non-resolver foundations: function inventory +37/+94/+107 (guarded and
def-like forms that simply did not exist in the graph before);
enclosing-function attribution 56→68 % / 70→76 % / 60→63 % (Phase 0); 100 % of
functions arity-identified (Phase 1c); IMPORTS edges pointing at real modules
instead of the root module (Phase 2.5d); two classes of **false** edges
eliminated (stdlib collisions, root-collapsed imports).

The showcase's 100 % is the demonstrated ceiling on clean code; the real-repo
remainder is measured and categorised below.

---

## 3. What this means for a user of the MCP tools

The tangible test is: *what can an agent or human do against an Elixir graph
now that it couldn't before?* Evidence is from the blind-graded `claude -p`
rubric runs at each checkpoint.

- **Finding functions (`search_graph`).** Before: guarded defs, `defmacro`,
  `defguard`, `defprotocol`/`defimpl` had **no nodes** — on plug, 24 of 82
  guarded functions were invisible; the rubric's guarded-def probe needed a
  7-tool cascade ending in `grep`. Now: complete inventory, and arity-distinct
  (`read_body/1` vs `read_body/2` are separate, queryable nodes). The
  baseline rubric concluded "arity is not represented"; the candidate found
  multi-arity families natively — FAIL → PASS.
- **Tracing calls (`trace_path`).** Before: call edges sourced from *modules*
  (useless for "which function calls X"), and arities had to be reconstructed
  by opening source — the E1 trace took **18 `get_code_snippet` calls** and
  still contained a false callee. Now: function-level, arity-precise traces in
  ~4 graph calls, with 54 %/48 %/37 % of edges carrying a module- and
  arity-verified strategy — and the notable false-edge classes removed.
- **Inbound "who calls X?" queries.** The trustworthiness question. Before:
  a stdlib call like `Keyword.get/3` could appear as a caller of your project's
  `get/3` (the C2 rubric caught exactly this). Now suppressed — inbound edges
  to project functions are either resolver-verified or honest textual matches,
  not stdlib noise.
- **Dependency structure (`get_architecture`, IMPORTS).** Before: plug's 92
  import edges **all pointed at the root `Plug` node** — the module dependency
  map was fiction. Now: 88 edges to the actual modules (`Plug.Conn` ×34,
  `Plug.Test` ×23, …), and the showcase resolves its in-repo directives to
  exactly the right Class nodes.
- **Agent behaviour under a strict MCP-only harness.** Graph-gap signals in
  the rubric fell from 3 questions to 1 at C1; the strict runner (no
  Bash/grep) answers 17/17 from the graph alone, where the baseline leaned on
  text fallback.

**Honest bottom line:** yes — the improvement is tangible and evidenced, not
cosmetic. An agent working a real Elixir repo through this MCP gets a complete
function inventory, arity-correct identity and traces, a majority-verified
call graph on library-style code, true import structure, and — as important —
*fewer wrong answers*, because the two systematic false-edge classes are gone.
What it does **not** get is listed next, plainly.

---

## 4. Limitations that remain

Grouped by kind. "By design" = the zero-edge discipline working as intended;
"deferred" = implementable, consciously parked; "inherent" = not statically
solvable without a running BEAM (Expert delegates these to the runtime too).

### Inherent to static analysis
1. **Dynamic dispatch** — `apply/3`, variable modules (`mod.fun(x)`),
   `Module.concat`, config-driven modules: no edge, by design. A runtime tool
   would see these; a static one that guessed would be wrong.
2. **Full macro expansion** — `quote`/`unquote` bodies and macros that
   *generate* defs beyond the curated `use` table are invisible. Calls inside
   `quote` blocks are treated as ordinary calls (a small noise source shared
   by every static analyser).
3. **Protocol dispatch is to the protocol, not the impl.** `Describable.
   describe(x)` resolves to the protocol's `describe/1` def — the concrete
   `defimpl` chosen at runtime is unknowable statically. (The impls themselves
   are indexed as `Protocol.Target` Classes with their functions.)

### Known false-edge sources still open
4. **Erlang atom-module calls** — `:ets.insert(t, v)` is *not* LSP-classified
   (atom modules are outside the curated tables), so the textual resolver can
   still fuzzy-match it to a same-file project `insert/2` (verified in a probe
   while writing this report). Same defect class the Phase-2.5c suppression
   fixed for Elixir stdlib — the fix is the same shape (classify or suppress
   atom-module callees) and is the top precision follow-up.
5. **Uncurated hex deps** — a call to `HTTPoison.get/1` (any dep outside the
   curated stdlib/framework tables) is unclassified; the textual matcher may
   fuzzy-match its short name to a project function. Rarer than #4 in
   practice (dotted names match less), but the same class.

### Deferred model gaps
6. **stdlib calls form no graph edges.** `Enum.map/2` is *classified*
   (`lsp_ex_stdlib`) but there is no `Enum.map/2` node, so no CALLS edge —
   "what stdlib does this function lean on?" is not answerable from the graph.
   Fix exists (mint curated stdlib nodes, the `kotlin_builtins.c` pattern) but
   inflates every project's node count; deliberately opt-in, not shipped.
7. **`@behaviour`/`@impl` linkage** — `handle_call/3` is a Function node but
   carries no OVERRIDE edge to the GenServer callback identity (needs the
   same node-injection decision as #6).
8. **Multi-module files collide.** Two `defmodule`s in one file share the
   path-based QN prefix, so same-named functions in them merge onto one node
   (a pre-existing store-model property; the "kind-disambiguated QNs" cure is
   a shared-code follow-up outside the Elixir gate).
9. **Selector atom forms** — `import Foo, only: :functions` (atom, not a
   keyword list) currently admits nothing (conservative: no false edges, but
   those imports resolve only via Kernel fallback).
10. **`__MODULE__.Sub` aliases** don't expand (the literal is stored, never
    matched); `defdelegate` bodies don't emit a delegation edge to their
    target.
11. **Templates** — `.heex`/`.eex` are not parsed as Elixir; calls made from
    templates are invisible.
12. **Same-arity multi-clause span** — the merged node keeps the last
    clause's line span (cosmetic).

### Scale note
13. The cross-file pass rebuilds a registry per file from the full def set
    (the Rust tradeoff). Flat at 1,200 files (analytics); a 10k-file monorepo
    would want the Tier-2 shared registry — the decision point already in
    Phase 3.

---

## 5. Further improvements possible (ranked by user value ÷ effort)

| # | Improvement | Closes | Effort |
|---|---|---|---|
| 1 | Suppress/classify **atom-module** (`:ets`, `:timer`) callees | limitation 4 — the last systematic false-edge class | small (2.5c shape) |
| 2 | `import … only:/except: :functions/:macros` atom selectors | limitation 9 | small |
| 3 | `@behaviour`/`@impl` OVERRIDE edges (+ injected callback nodes) | limitation 7 — behaviour navigation | medium |
| 4 | Opt-in stdlib node injection → stdlib CALLS edges | limitation 6 — "uses Enum/GenServer" queries | medium (opt-in flag + golden updates) |
| 5 | `defdelegate` delegation edges; `__MODULE__.Sub` aliases | limitation 10 | small |
| 6 | Tier-2 shared cross-file registry | limitation 13 at monorepo scale | medium |
| 7 | Kind-disambiguated QNs (multi-module files) | limitation 8 | large, shared-code |
| 8 | `.heex` template call extraction | limitation 11 | medium |
| 9 | `@spec` hints | typed navigation | large, plan non-goal unless justified |

Items 1–2 are the natural "Phase 2.6" if further precision is wanted before
promotion; items 3–4 are the next capability step; the rest are roadmap.

---

## 6. Verification status

- **Unit/probe:** 31 `test_elixir_lsp` + 10 `lrp_elixir_s1..s10` scenarios;
  full suite **6,657 passed / 0 failed / 4 skipped** (ASan + UBSan) at the tip.
- **Determinism oracle:** the pinned `elixir_showcase` — 8/8 node capability
  checks, 6/6 resolver-verified CALLS, 2/2 correct IMPORTS targets.
- **Cross-language safety:** TypeScript control byte-identical at every
  checkpoint pair; all changes language- or extension-gated.
- **Thread safety:** `make test-tsan` run for this report — result below.
- **Clean-room:** originality scan CLEAN against Expert (and all references)
  at every resolver PR.

**TSan result:** `make test-tsan` run 2026-07-24 at the Phase-2.5 tip —
**755 passed / 0 failed / 3 skipped, zero ThreadSanitizer warnings** (exit 0).
The TSan leg exercises the concurrency suites, including the parallel pipeline
under which the Elixir cross-file resolver and the Phase-2.5c suppression run;
the resolver's per-call scratch arenas are confirmed thread-confined. (The
full-battery TSan soak and the Windows leg remain PR-3a items.)

---

## 7. Assessment

The work took Elixir from a syntactic sketch — missing a quarter of plug's
guarded functions, attributing calls to modules, one node per function name
regardless of arity, zero semantic resolution, and a dependency map that
pointed everything at the root module — to a graph where the inventory is
complete, identity is arity-precise, a majority of library-code call edges are
module- and arity-verified, imports point at their true targets, and the
resolver **refuses to guess** where it cannot know. The gaps that remain are
either inherent to static analysis (and shared with Expert's static layer),
honestly enumerated precision items with known fixes (§5, items 1–2 first), or
deliberate scope decisions with recorded rationale. For a user pointing an
agent at an Elixir codebase through this MCP, the difference is measurable in
the tool transcripts: fewer fallback cascades, arity-correct answers, and —
most valuable in practice — substantially fewer *confidently wrong* edges.
