/*
 * elixir_lsp.c — Elixir Light Semantic Pass.
 *
 * In-process semantic call resolver for Elixir. Mirrors the perl_lsp.c /
 * php_lsp.c / go_lsp.c shape:
 *   1. Build a CBMTypeRegistry from file-local definitions + stdlib (Kernel
 *      auto-imports + curated core modules).
 *   2. elixir_lsp_process_file walks module + def bodies, building a scope of
 *      alias/import/require/use directives, and resolves call expressions into
 *      CBMResolvedCall edges via the resolution ladder (Phase 1b).
 *
 * PR-1a (this commit) ships the SKELETON only: init/entry lifecycle and a
 * depth-guarded no-op walk that emits nothing. The graph output is therefore
 * byte-identical to the pre-resolver baseline. PR-1b fills in scope/alias
 * collection and the qualified/local resolution rungs; PR-1c adds name/arity
 * identity, pipes and captures.
 *
 * Verified tree-sitter-elixir node/field names (confirmed against the vendored
 * compiled grammar at internal/cbm/vendored/grammars/elixir/parser.c and the
 * Phase-0 extractors that already consume them):
 *   - `def`/`defp`/`defmodule`/`alias`/`import`/`use`/... are all `call` nodes
 *     whose first child is an `identifier` target (the macro keyword). This is
 *     the tags.scm recognition model; the Phase-0 predicate
 *     cbm_elixir_def_macro() (helpers.c) is the shared def gate.
 *   - a guarded head `def foo(x) when g` is a `call` whose args' first child is
 *     a `binary_operator` (the `when`); descend to its `left`.
 *   - qualified calls `Mod.fun(args)` present the callee as a `dot` node;
 *     local calls present it as a bare `identifier`.
 *
 * QN scheme: the structural extractor names each def via cbm_fqn_compute
 * (path-based); the module name is not woven into the Function QN. See the
 * header for the rationale and the Phase-2b cross-file plan.
 *
 * Zero-edge guarantee: an unresolvable callee emits NO edge. Dynamic dispatch
 * (`apply/3`, variable modules, `unquote`) is intentionally left unresolved.
 */

#include "elixir_lsp.h"
#include "lsp_node_iter.h"
#include "../arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolution confidence levels (mirror perl_lsp's tiers; both above the 0.6
 * pipeline floor in src/pipeline/lsp_resolve.h). Consumed from PR-1b onward. */
#define ELIXIR_CONF_LITERAL 0.95f  /* qualified/alias-expanded registry hit */
#define ELIXIR_CONF_INFERRED 0.75f /* import/Kernel auto-import fallthrough */

/* Maximum AST-walk recursion depth. Mirrors CBM_LSP_PERL_MAX_WALK_DEPTH: the
 * per-child recursion can stack-overflow on pathologically nested sources; past
 * the cap the subtree is skipped (graceful degradation, never a crash). The
 * zero-edge guarantee is preserved — a skipped subtree emits no edges. */
#define CBM_LSP_ELIXIR_MAX_WALK_DEPTH 512

/* ── public API: init ───────────────────────────────────────────── */

void elixir_lsp_init(ElixirLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                     const CBMTypeRegistry *registry, const char *module_qn,
                     CBMResolvedCallArray *out) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena;
    ctx->source = source;
    ctx->source_len = source_len;
    ctx->registry = registry;
    ctx->module_qn = module_qn;
    ctx->current_module_qn = "";
    ctx->enclosing_module_qn = "";
    ctx->resolved_calls = out;
    ctx->current_scope = cbm_scope_push(arena, NULL);

    const char *dbg = getenv("CBM_LSP_DEBUG");
    ctx->debug = (dbg && dbg[0]);
}

/* ── body walk ──────────────────────────────────────────────────── */

/* PR-1a scaffold: a depth-guarded descent that visits every named node but
 * emits nothing. PR-1b hangs module/def scope handling and the call-resolution
 * ladder off this walk. Kept minimal-but-real so the depth guard and the O(n)
 * child collector are exercised from the start. */
void elixir_lsp_process_file(ElixirLSPContext *ctx, TSNode root) {
    if (ts_node_is_null(root))
        return;
    if (ctx->walk_depth >= CBM_LSP_ELIXIR_MAX_WALK_DEPTH)
        return;
    ctx->walk_depth++;

    uint32_t nc = 0;
    TSNode *kids = cbm_lsp_collect_children(ctx->arena, root, &nc);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = kids ? kids[i] : ts_node_child(root, i);
        if (ts_node_is_null(c) || !ts_node_is_named(c))
            continue;
        /* PR-1b: dispatch defmodule / def / alias / import / call here. */
        elixir_lsp_process_file(ctx, c);
    }

    ctx->walk_depth--;
}

/* ── entry: cbm_run_elixir_lsp ──────────────────────────────────── */

void cbm_run_elixir_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                        TSNode root) {
    if (!result || !arena || ts_node_is_null(root))
        return;

    CBMTypeRegistry reg;
    cbm_registry_init(&reg, arena);

    /* Phase A: register stdlib (Kernel auto-imports + curated core modules). */
    cbm_elixir_stdlib_register(&reg, arena);

    const char *module_qn = result->module_qn;

    /* Phase B: register file-local defs (label Function/Method). Return types
     * are unknown — Elixir defs carry no declared return type at this layer. */
    for (int i = 0; i < result->defs.count; i++) {
        CBMDefinition *d = &result->defs.items[i];
        if (!d->qualified_name || !d->name || !d->label)
            continue;
        if (strcmp(d->label, "Function") == 0 || strcmp(d->label, "Method") == 0) {
            CBMRegisteredFunc rf;
            memset(&rf, 0, sizeof(rf));
            rf.qualified_name = d->qualified_name;
            rf.short_name = d->name;
            if (strcmp(d->label, "Method") == 0 && d->parent_class)
                rf.receiver_type = d->parent_class;
            const CBMType **rets =
                (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(const CBMType *));
            if (rets) {
                rets[0] = cbm_type_unknown();
                rets[1] = NULL;
            }
            rf.signature = cbm_type_func(arena, NULL, NULL, rets);
            cbm_registry_add_func(&reg, rf);
        }
    }

    /* Finalize the registry for O(1) lookups during resolution. Bucket
     * allocations go to a per-call scratch arena that dies with this call
     * rather than accumulating on the pipeline-lifetime result arena. */
    CBMArena idx_arena;
    cbm_arena_init(&idx_arena);
    cbm_registry_finalize_into(&reg, &idx_arena);

    ElixirLSPContext ctx;
    elixir_lsp_init(&ctx, arena, source, source_len, &reg, module_qn, &result->resolved_calls);

    /* Phase C: resolution walk. PR-1a: no-op (emits nothing). */
    elixir_lsp_process_file(&ctx, root);

    if (ctx.debug) {
        fprintf(stderr, "[elixir_lsp] module_qn=%s defs=%d resolved=%d\n",
                module_qn ? module_qn : "(null)", result->defs.count, result->resolved_calls.count);
    }

    cbm_arena_destroy(&idx_arena);
}
