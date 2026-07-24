/*
 * elixir_lsp.c — Elixir Light Semantic Pass.
 *
 * In-process semantic call resolver for Elixir. Mirrors the perl_lsp.c /
 * php_lsp.c / go_lsp.c shape:
 *   1. Build a CBMTypeRegistry from file-local definitions + stdlib (Kernel
 *      auto-imports + curated core modules).
 *   2. elixir_lsp_process_file does a TWO-PASS walk:
 *        PASS 1 — collect module definitions (`defmodule`, nesting joined) and
 *          line-ordered directives (`alias`/`require ..., as:`, `import`,
 *          `use`), incl. multi-alias `Foo.{Bar, Baz}` and `as:`.
 *        PASS 2 — walk each def body, tracking the enclosing def QN, and resolve
 *          local (`fun(args)`) and qualified (`Mod.fun(args)`) calls into
 *          CBMResolvedCall edges.
 *
 * Verified tree-sitter-elixir node shapes (confirmed against the Phase-0
 * extractors that already consume them — extract_defs.c / extract_imports.c):
 *   - `def`/`defp`/`defmodule`/`alias`/`import`/`use`/... are all `call` nodes
 *     whose first child is an `identifier` target (the macro keyword). The
 *     Phase-0 predicate cbm_elixir_def_macro() (helpers.c) is the shared def
 *     gate.
 *   - a def head is the call's first argument: a `call` `name(params)`, a bare
 *     `identifier` (zero-arity), or a `binary_operator` (`when`-guarded) whose
 *     `left` is the head. The `do...end` body is a `do_block` child of the call
 *     (or a `do:` keyword in the arguments).
 *   - qualified calls `Mod.fun(args)` present the callee as a `dot` node; local
 *     calls present it as a bare `identifier`.
 *
 * QN scheme: the structural extractor names each def via cbm_fqn_compute — the
 * QN is `<module_qn>.<name>/<arity>` where module_qn is the file's path-based
 * module (the Elixir module name is NOT woven in; that is Class-node territory,
 * D6) and `/arity` is the name/arity identity (D3). Every def in a file shares
 * the module_qn prefix, so a def resolves via
 * cbm_registry_lookup_symbol(module_qn, "name/arity") whether the call was
 * written bare or qualified against a same-file module. Cross-file /
 * cross-module resolution (mapping a dotted module name to another file's defs)
 * is Phase 2b.
 *
 * Resolution ladder:
 *   - local `fun(args)`      → file-local def, else Kernel builtin
 *                              → lsp_ex_local / lsp_ex_kernel
 *   - qualified `Mod.fun(a)` → Mod is a file-defined module or `__MODULE__`
 *                              → file-local def (lsp_ex_qualified);
 *                              else a curated stdlib module (Enum/Map/GenServer…)
 *                              → lsp_ex_stdlib
 * Everything else — a qualified call to an unknown cross-file project module, a
 * variable-module dispatch, `apply/3`, `unquote` — emits NO edge (Phase 2b or
 * genuinely dynamic). Name/arity identity (Phase 1c) suffixes every def QN and
 * lookup with `/arity` (pipes add +1, captures `&f/N` carry the literal), via
 * the shared cbm_elixir_*_arity helpers. Import only:/except: selector
 * resolution is Phase 2.
 *
 * Zero-edge guarantee: an unresolvable callee emits NO edge (a false edge is
 * worse than a missing one).
 */

#include "elixir_lsp.h"
#include "lsp_node_iter.h"
#include "../helpers.h"
#include "../arena.h"
#include "../../../src/foundation/hash_table.h" /* CBMHashTable (cross module map) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tree-sitter-elixir language (vendored) — for the cross pass to re-parse a
 * file when the pipeline did not hand over a cached tree. */
extern const TSLanguage *tree_sitter_elixir(void);

/* Resolution confidence levels (mirror perl_lsp's tiers; both above the 0.6
 * pipeline floor in src/pipeline/lsp_resolve.h). */
#define ELIXIR_CONF_LOCAL 0.90f     /* same-module / file-local def hit */
#define ELIXIR_CONF_QUALIFIED 0.90f /* alias-gated same-file qualified hit */
#define ELIXIR_CONF_STDLIB 0.85f    /* curated Kernel / core-module hit */
#define ELIXIR_CONF_CROSS 0.85f     /* cross-file project-module hit (Phase 2b) */

/* Maximum AST-walk recursion depth. Mirrors CBM_LSP_PERL_MAX_WALK_DEPTH: the
 * per-child recursion can stack-overflow on pathologically nested sources; past
 * the cap the subtree is skipped (graceful degradation, never a crash). The
 * zero-edge guarantee is preserved — a skipped subtree emits no edges. */
#define CBM_LSP_ELIXIR_MAX_WALK_DEPTH 512

/* Alias-expansion chase bound (transitive `alias A.B` then `alias B.C`). */
#define ELIXIR_ALIAS_MAX_CHASE 8

/* ── forward declarations ───────────────────────────────────────── */

static void elixir_scan_directives(ElixirLSPContext *ctx, TSNode node, const char *mod_chain);
static void elixir_resolve_walk(ElixirLSPContext *ctx, TSNode node);

/* ── helpers ────────────────────────────────────────────────────── */

static char *elixir_node_text(ElixirLSPContext *ctx, TSNode node) {
    return cbm_node_text(ctx->arena, node, ctx->source);
}

/* The `arguments` node of a `call` (field "arguments", or child 1 fallback). */
static TSNode elixir_call_args(TSNode call) {
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args) && ts_node_child_count(call) > 1)
        args = ts_node_child(call, 1);
    return args;
}

/* Target text of a `call` (its first child — the macro keyword or local callee
 * identifier). Returns NULL when the first child is not an identifier (e.g. a
 * qualified `dot` callee). */
static char *elixir_call_target_ident(ElixirLSPContext *ctx, TSNode call) {
    if (ts_node_child_count(call) == 0)
        return NULL;
    TSNode t = ts_node_child(call, 0);
    if (ts_node_is_null(t) || strcmp(ts_node_type(t), "identifier") != 0)
        return NULL;
    return elixir_node_text(ctx, t);
}

/* Find the `do_block` child of a call node (the def/module body). */
static TSNode elixir_find_do_block(TSNode call) {
    uint32_t n = ts_node_child_count(call);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(call, i);
        if (!ts_node_is_null(c) && strcmp(ts_node_type(c), "do_block") == 0)
            return c;
    }
    TSNode null_node;
    memset(&null_node, 0, sizeof(null_node));
    return null_node;
}

static bool elixir_is_directive_kw(const char *kw) {
    return kw && (strcmp(kw, "alias") == 0 || strcmp(kw, "import") == 0 ||
                  strcmp(kw, "require") == 0 || strcmp(kw, "use") == 0);
}

/* First dotted segment of `mod` (up to the first '.'), arena-copied. */
static char *elixir_first_segment(CBMArena *a, const char *mod) {
    const char *dot = strchr(mod, '.');
    size_t len = dot ? (size_t)(dot - mod) : strlen(mod);
    return cbm_arena_strndup(a, mod, len);
}

/* ── PASS 1: module + directive collection ──────────────────────── */

static void elixir_grow_alias(ElixirLSPContext *ctx) {
    if (ctx->alias_count < ctx->alias_cap)
        return;
    int nc = ctx->alias_cap ? ctx->alias_cap * 2 : 8;
    const char **nl = cbm_arena_alloc(ctx->arena, (size_t)nc * sizeof(char *));
    const char **nt = cbm_arena_alloc(ctx->arena, (size_t)nc * sizeof(char *));
    int *nline = cbm_arena_alloc(ctx->arena, (size_t)nc * sizeof(int));
    if (!nl || !nt || !nline)
        return;
    for (int i = 0; i < ctx->alias_count; i++) {
        nl[i] = ctx->alias_local[i];
        nt[i] = ctx->alias_target[i];
        nline[i] = ctx->alias_line[i];
    }
    ctx->alias_local = nl;
    ctx->alias_target = nt;
    ctx->alias_line = nline;
    ctx->alias_cap = nc;
}

/* Expand `mod`'s first segment against the alias map (aliases at line <=
 * `line`, later overrides), chasing transitively up to a small bound. */
static const char *elixir_expand_alias(ElixirLSPContext *ctx, const char *mod, int line) {
    const char *cur = mod;
    for (int chase = 0; chase < ELIXIR_ALIAS_MAX_CHASE; chase++) {
        char *seg0 = elixir_first_segment(ctx->arena, cur);
        const char *target = NULL;
        int best_line = -1;
        for (int i = 0; i < ctx->alias_count; i++) {
            if (ctx->alias_line[i] <= line && strcmp(ctx->alias_local[i], seg0) == 0 &&
                ctx->alias_line[i] > best_line) {
                target = ctx->alias_target[i];
                best_line = ctx->alias_line[i];
            }
        }
        if (!target)
            return cur;
        size_t seg0len = strlen(seg0);
        cur = cur[seg0len] ? cbm_arena_sprintf(ctx->arena, "%s%s", target, cur + seg0len) : target;
    }
    return cur;
}

static void elixir_add_alias(ElixirLSPContext *ctx, const char *local, const char *target,
                             int line) {
    if (!local || !local[0] || !target || !target[0])
        return;
    /* Expand the target transitively against aliases seen so far (record-time
     * chaining: `alias A.B` then `alias B.C` → C -> A.B.C). */
    const char *expanded = elixir_expand_alias(ctx, target, line);
    elixir_grow_alias(ctx);
    if (ctx->alias_count >= ctx->alias_cap)
        return;
    ctx->alias_local[ctx->alias_count] = cbm_arena_strdup(ctx->arena, local);
    ctx->alias_target[ctx->alias_count] = cbm_arena_strdup(ctx->arena, expanded);
    ctx->alias_line[ctx->alias_count] = line;
    ctx->alias_count++;
}

static void elixir_add_defined_module(ElixirLSPContext *ctx, const char *mod) {
    if (!mod || !mod[0])
        return;
    for (int i = 0; i < ctx->defined_module_count; i++)
        if (strcmp(ctx->defined_modules[i], mod) == 0)
            return;
    if (ctx->defined_module_count >= ctx->defined_module_cap) {
        int nc = ctx->defined_module_cap ? ctx->defined_module_cap * 2 : 8;
        const char **nm = cbm_arena_alloc(ctx->arena, (size_t)nc * sizeof(char *));
        if (!nm)
            return;
        for (int i = 0; i < ctx->defined_module_count; i++)
            nm[i] = ctx->defined_modules[i];
        ctx->defined_modules = nm;
        ctx->defined_module_cap = nc;
    }
    ctx->defined_modules[ctx->defined_module_count++] = cbm_arena_strdup(ctx->arena, mod);
}

static bool elixir_module_defined(ElixirLSPContext *ctx, const char *mod) {
    for (int i = 0; i < ctx->defined_module_count; i++)
        if (strcmp(ctx->defined_modules[i], mod) == 0)
            return true;
    return false;
}

/* Record an `as: Alias` value among a directive's trailing keyword args. */
static const char *elixir_find_as_alias(ElixirLSPContext *ctx, TSNode args) {
    uint32_t ac = ts_node_child_count(args);
    for (uint32_t i = 1; i < ac; i++) {
        TSNode kw = ts_node_child(args, i);
        if (ts_node_is_null(kw) || strcmp(ts_node_type(kw), "keywords") != 0)
            continue;
        uint32_t pc = ts_node_child_count(kw);
        for (uint32_t j = 0; j < pc; j++) {
            TSNode pair = ts_node_child(kw, j);
            if (ts_node_is_null(pair) || ts_node_child_count(pair) < 2)
                continue;
            char *key = elixir_node_text(ctx, ts_node_child(pair, 0));
            if (key && strncmp(key, "as", 2) == 0)
                return elixir_node_text(ctx, ts_node_child(pair, ts_node_child_count(pair) - 1));
        }
    }
    return NULL;
}

/* Last dotted segment of `mod` ("Foo.Bar.Baz" → "Baz"). */
static const char *elixir_last_segment(CBMArena *a, const char *mod) {
    const char *dot = strrchr(mod, '.');
    return dot ? cbm_arena_strdup(a, dot + 1) : cbm_arena_strdup(a, mod);
}

/* Expand `Base.{A, B.C}` into aliases Base.A (local A) and Base.B.C (local C).
 * Returns true if it was a multi-alias form. */
static bool elixir_expand_multi_alias(ElixirLSPContext *ctx, char *mtext, int line) {
    char *brace = strchr(mtext, '{');
    if (!brace)
        return false;
    char *base = cbm_arena_strndup(ctx->arena, mtext, (size_t)(brace - mtext));
    size_t bl = strlen(base);
    while (bl > 0 && (base[bl - 1] == '.' || base[bl - 1] == ' '))
        base[--bl] = '\0';
    char *inner = brace + 1;
    char *close = strchr(inner, '}');
    if (close)
        *close = '\0';
    char *save = NULL;
    for (char *tok = strtok_r(inner, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ')
            tok++;
        size_t tl = strlen(tok);
        while (tl > 0 && tok[tl - 1] == ' ')
            tok[--tl] = '\0';
        if (!tok[0])
            continue;
        char *full = base[0] ? cbm_arena_sprintf(ctx->arena, "%s.%s", base, tok) : tok;
        elixir_add_alias(ctx, elixir_last_segment(ctx->arena, full), full, line);
    }
    return true;
}

/* Collect a `defmodule`/`defprotocol` module name (nesting joined via
 * mod_chain), an `alias`/`require ..., as:`, `import`, or `use` directive. */
static void elixir_scan_directives(ElixirLSPContext *ctx,
                                   TSNode node, // NOLINT(misc-no-recursion)
                                   const char *mod_chain) {
    if (ts_node_is_null(node) || ctx->walk_depth >= CBM_LSP_ELIXIR_MAX_WALK_DEPTH)
        return;
    ctx->walk_depth++;

    const char *child_chain = mod_chain;
    if (strcmp(ts_node_type(node), "call") == 0) {
        char *kw = elixir_call_target_ident(ctx, node);
        if (kw && (strcmp(kw, "defmodule") == 0 || strcmp(kw, "defprotocol") == 0)) {
            TSNode args = elixir_call_args(node);
            if (!ts_node_is_null(args) && ts_node_child_count(args) > 0) {
                char *name = elixir_node_text(ctx, ts_node_child(args, 0));
                if (name && name[0]) {
                    char *full =
                        mod_chain ? cbm_arena_sprintf(ctx->arena, "%s.%s", mod_chain, name) : name;
                    elixir_add_defined_module(ctx, full);
                    child_chain = full;
                }
            }
        } else if (elixir_is_directive_kw(kw)) {
            TSNode args = elixir_call_args(node);
            if (!ts_node_is_null(args) && ts_node_child_count(args) > 0) {
                int line = (int)ts_node_start_point(node).row + 1;
                char *mtext = elixir_node_text(ctx, ts_node_child(args, 0));
                if (mtext && mtext[0]) {
                    if (strcmp(kw, "alias") == 0 && elixir_expand_multi_alias(ctx, mtext, line)) {
                        /* multi-alias handled */
                    } else if (strcmp(kw, "alias") == 0 || strcmp(kw, "require") == 0) {
                        const char *as = elixir_find_as_alias(ctx, args);
                        elixir_add_alias(ctx, as ? as : elixir_last_segment(ctx->arena, mtext),
                                         mtext, line);
                    } else if (strcmp(kw, "import") == 0) {
                        if (ctx->import_count < 64) {
                            if (ctx->import_count >= ctx->import_cap) {
                                int ncp = ctx->import_cap ? ctx->import_cap * 2 : 8;
                                const char **ni =
                                    cbm_arena_alloc(ctx->arena, (size_t)ncp * sizeof(char *));
                                if (ni) {
                                    for (int i = 0; i < ctx->import_count; i++)
                                        ni[i] = ctx->import_module[i];
                                    ctx->import_module = ni;
                                    ctx->import_cap = ncp;
                                }
                            }
                            if (ctx->import_count < ctx->import_cap)
                                ctx->import_module[ctx->import_count++] =
                                    cbm_arena_strdup(ctx->arena, mtext);
                        }
                    }
                    /* `use` recorded structurally; expansion is Phase 2c. */
                }
            }
        }
    }

    uint32_t nc = 0;
    TSNode *kids = cbm_lsp_collect_children(ctx->arena, node, &nc);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = kids ? kids[i] : ts_node_child(node, i);
        if (!ts_node_is_null(c) && ts_node_is_named(c))
            elixir_scan_directives(ctx, c, child_chain);
    }
    ctx->walk_depth--;
}

/* ── PASS 2: emit + call resolution ─────────────────────────────── */

static void elixir_emit(ElixirLSPContext *ctx, const char *callee_qn, const char *strategy,
                        float confidence) {
    if (!ctx->resolved_calls || !callee_qn || !ctx->enclosing_func_qn)
        return;
    CBMResolvedCall rc;
    rc.caller_qn = ctx->enclosing_func_qn;
    rc.callee_qn = callee_qn;
    rc.strategy = strategy;
    rc.confidence = confidence;
    rc.reason = NULL;
    cbm_resolvedcall_push(ctx->resolved_calls, ctx->arena, rc);
}

/* Call-site arity at a call/capture node (name/arity identity, D3). */
static int elixir_callsite_arity(ElixirLSPContext *ctx, TSNode node) {
    int arity = 0;
    if (!cbm_elixir_capture_arity(node, ctx->source, &arity))
        arity = cbm_elixir_call_arity(node, ctx->source);
    return arity;
}

/* Resolve a local `fun(args)` call: first against the file-local module, then
 * the Kernel auto-import (is_atom/1, elem/2, length/1, …). The registry key is
 * `fun/arity` — def QNs are arity-suffixed (D3), so the lookup must be too. */
static void elixir_resolve_local(ElixirLSPContext *ctx, TSNode node, const char *fun) {
    if (!fun || !fun[0] || !ctx->module_qn)
        return;
    const char *key = cbm_arena_sprintf(ctx->arena, "%s/%d", fun, elixir_callsite_arity(ctx, node));
    const CBMRegisteredFunc *f = cbm_registry_lookup_symbol(ctx->registry, ctx->module_qn, key);
    if (f && f->qualified_name) {
        elixir_emit(ctx, f->qualified_name, "lsp_ex_local", ELIXIR_CONF_LOCAL);
        return;
    }
    /* Kernel auto-import fallback — a bare call not defined in this module may
     * be a Kernel builtin. (Classifies the call; forms a graph edge only if a
     * Kernel node exists — see elixir_stdlib_data.c scope note.) */
    const CBMRegisteredFunc *k = cbm_registry_lookup_symbol(ctx->registry, "Kernel", key);
    if (k && k->qualified_name)
        elixir_emit(ctx, k->qualified_name, "lsp_ex_kernel", ELIXIR_CONF_STDLIB);
}

/* Resolve a qualified `Mod.fun(args)` call. Mod is alias-expanded; if it is a
 * module defined in this file (or __MODULE__), resolve against the file-local
 * defs (lsp_ex_qualified). Otherwise, if Mod.fun/arity is a curated stdlib
 * entry (Enum, Map, GenServer, …), classify it lsp_ex_stdlib. A cross-file
 * project module falls through to zero-edge here — that is Phase 2b. The
 * registry key is `fun/arity` (D3). */
static void elixir_resolve_qualified(ElixirLSPContext *ctx, TSNode node, const char *callee,
                                     int line) {
    const char *dot = strrchr(callee, '.');
    if (!dot || dot == callee)
        return;
    char *mod = cbm_arena_strndup(ctx->arena, callee, (size_t)(dot - callee));
    const char *fun = dot + 1;
    if (!fun[0] || !ctx->module_qn)
        return;

    /* Dynamic dispatch on a variable receiver (lowercase first char) is never
     * resolvable — zero-edge. */
    if (mod[0] && (mod[0] < 'A' || mod[0] > 'Z') && strcmp(mod, "__MODULE__") != 0)
        return;

    const char *key = cbm_arena_sprintf(ctx->arena, "%s/%d", fun, elixir_callsite_arity(ctx, node));

    bool same_module = (strcmp(mod, "__MODULE__") == 0);
    const char *expanded = same_module ? NULL : elixir_expand_alias(ctx, mod, line);
    if (!same_module && expanded)
        same_module = elixir_module_defined(ctx, expanded);

    if (same_module) {
        const CBMRegisteredFunc *f = cbm_registry_lookup_symbol(ctx->registry, ctx->module_qn, key);
        if (f && f->qualified_name)
            elixir_emit(ctx, f->qualified_name, "lsp_ex_qualified", ELIXIR_CONF_QUALIFIED);
        return;
    }

    /* Phase 2b: cross-file project module. The map (Elixir module name ->
     * def_module_qn path prefix) is populated only by cbm_run_elixir_lsp_cross,
     * so this block is inert in the per-file pass (Phase 1 preserved). */
    if (ctx->cross_module_map && expanded) {
        const char *dmq = (const char *)cbm_ht_get((CBMHashTable *)ctx->cross_module_map, expanded);
        if (dmq) {
            const CBMRegisteredFunc *f = cbm_registry_lookup_symbol(ctx->registry, dmq, key);
            if (f && f->qualified_name) {
                elixir_emit(ctx, f->qualified_name, "lsp_ex_cross", ELIXIR_CONF_CROSS);
                return;
            }
        }
    }

    /* Curated stdlib module (Enum/Map/String/GenServer/…) — classify. */
    const CBMRegisteredFunc *s =
        cbm_registry_lookup_symbol(ctx->registry, expanded ? expanded : mod, key);
    if (s && s->qualified_name)
        elixir_emit(ctx, s->qualified_name, "lsp_ex_stdlib", ELIXIR_CONF_STDLIB);
    /* else: cross-file project module → zero-edge (Phase 2b). */
}

/* Resolve every call in an expression subtree (a def head is excluded by the
 * caller). Recurses so calls nested in arguments (`foo(bar())`) are seen. */
static void elixir_resolve_calls_in(ElixirLSPContext *ctx,
                                    TSNode node) { // NOLINT(misc-no-recursion)
    if (ts_node_is_null(node) || ctx->walk_depth >= CBM_LSP_ELIXIR_MAX_WALK_DEPTH)
        return;
    ctx->walk_depth++;

    if (strcmp(ts_node_type(node), "call") == 0 && ts_node_child_count(node) > 0) {
        TSNode target = ts_node_child(node, 0);
        const char *tk = ts_node_type(target);
        if (strcmp(tk, "identifier") == 0) {
            char *name = elixir_node_text(ctx, target);
            /* def-macros and directives are not calls; `apply` is dynamic. */
            if (name && name[0] && !cbm_elixir_def_macro(name) && !elixir_is_directive_kw(name) &&
                strcmp(name, "defmodule") != 0 && strcmp(name, "defimpl") != 0 &&
                strcmp(name, "apply") != 0)
                elixir_resolve_local(ctx, node, name);
        } else if (strcmp(tk, "dot") == 0) {
            char *callee = elixir_node_text(ctx, target);
            if (callee && callee[0])
                elixir_resolve_qualified(ctx, node, callee, (int)ts_node_start_point(node).row + 1);
        }
    }

    uint32_t nc = 0;
    TSNode *kids = cbm_lsp_collect_children(ctx->arena, node, &nc);
    for (uint32_t i = 0; i < nc; i++) {
        TSNode c = kids ? kids[i] : ts_node_child(node, i);
        if (!ts_node_is_null(c) && ts_node_is_named(c))
            elixir_resolve_calls_in(ctx, c);
    }
    ctx->walk_depth--;
}

/* Walk the tree; on a def, set the enclosing-func QN and resolve its body; on a
 * module/other node, descend. */
static void elixir_resolve_walk(ElixirLSPContext *ctx, TSNode node) { // NOLINT(misc-no-recursion)
    if (ts_node_is_null(node) || ctx->walk_depth >= CBM_LSP_ELIXIR_MAX_WALK_DEPTH)
        return;
    ctx->walk_depth++;

    bool handled_children = false;
    if (strcmp(ts_node_type(node), "call") == 0) {
        char *kw = elixir_call_target_ident(ctx, node);
        if (kw && cbm_elixir_def_macro(kw)) {
            /* A def: derive its name (mirrors compute_elixir_func_qn), set the
             * enclosing QN (= module_qn.name, matching the extractor), and
             * resolve calls in the body only (not the head). */
            TSNode args = elixir_call_args(node);
            char *name = NULL;
            if (!ts_node_is_null(args) && ts_node_child_count(args) > 0) {
                TSNode first = ts_node_child(args, 0);
                const char *fk = ts_node_type(first);
                if (strcmp(fk, "binary_operator") == 0) {
                    TSNode left = ts_node_child_by_field_name(first, "left", 4);
                    if (!ts_node_is_null(left)) {
                        first = left;
                        fk = ts_node_type(first);
                    }
                }
                if (strcmp(fk, "call") == 0 && ts_node_child_count(first) > 0)
                    name = elixir_node_text(ctx, ts_node_child(first, 0));
                else if (strcmp(fk, "identifier") == 0)
                    name = elixir_node_text(ctx, first);
            }
            if (name && name[0] && ctx->module_qn) {
                const char *saved = ctx->enclosing_func_qn;
                /* Caller QN = module_qn.name/max-arity, byte-identical to the
                 * def node (extract_defs.c) and compute_elixir_func_qn — the
                 * LSP↔textual join is an exact strcmp on the caller QN. */
                int mn = 0;
                int mx = cbm_elixir_def_arity(node, ctx->source, &mn);
                ctx->enclosing_func_qn =
                    cbm_arena_sprintf(ctx->arena, "%s.%s/%d", ctx->module_qn, name, mx);
                /* Resolve calls in the body: the do_block, and any keyword-form
                 * body / guard-free trailing args (skip the head at index 0). */
                TSNode body = elixir_find_do_block(node);
                if (!ts_node_is_null(body))
                    elixir_resolve_calls_in(ctx, body);
                if (!ts_node_is_null(args)) {
                    uint32_t an = ts_node_child_count(args);
                    for (uint32_t i = 1; i < an; i++) {
                        TSNode c = ts_node_child(args, i);
                        if (!ts_node_is_null(c) && ts_node_is_named(c))
                            elixir_resolve_calls_in(ctx, c);
                    }
                }
                ctx->enclosing_func_qn = saved;
                handled_children = true;
            }
        }
    }

    if (!handled_children) {
        uint32_t nc = 0;
        TSNode *kids = cbm_lsp_collect_children(ctx->arena, node, &nc);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode c = kids ? kids[i] : ts_node_child(node, i);
            if (!ts_node_is_null(c) && ts_node_is_named(c))
                elixir_resolve_walk(ctx, c);
        }
    }
    ctx->walk_depth--;
}

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

/* ── process_file: two-pass walk ────────────────────────────────── */

void elixir_lsp_process_file(ElixirLSPContext *ctx, TSNode root) {
    if (ts_node_is_null(root))
        return;
    /* PASS 1: modules + directives. */
    ctx->walk_depth = 0;
    elixir_scan_directives(ctx, root, NULL);
    /* PASS 2: resolve + emit. */
    ctx->walk_depth = 0;
    elixir_resolve_walk(ctx, root);
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
        const CBMDefinition *d = &result->defs.items[i];
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

    /* Phase C: two-pass resolution walk. */
    elixir_lsp_process_file(&ctx, root);

    if (ctx.debug) {
        fprintf(stderr,
                "[elixir_lsp] module_qn=%s defs=%d resolved=%d aliases=%d modules=%d imports=%d\n",
                module_qn ? module_qn : "(null)", result->defs.count, result->resolved_calls.count,
                ctx.alias_count, ctx.defined_module_count, ctx.import_count);
        for (int i = 0; i < result->resolved_calls.count; i++) {
            const CBMResolvedCall *r = &result->resolved_calls.items[i];
            fprintf(stderr, "[elixir_lsp]   %s -> %s [%s %.2f]\n", r->caller_qn, r->callee_qn,
                    r->strategy, r->confidence);
        }
    }

    cbm_arena_destroy(&idx_arena);
}

/* ── entry: cbm_run_elixir_lsp_cross (Phase 2b) ─────────────────── */

/* Cross-file resolver. Mirrors cbm_run_kotlin_lsp_cross: register the
 * project-wide defs[] (filtered to this file's language, filter-exempt so the
 * WHOLE project is present — Elixir module names are globally unique) into a
 * scratch registry, recover the module-identity map from Class defs, then run
 * the shared two-pass walk with the cross map enabled so a `Mod.fun/arity`
 * call to another file resolves to that module's def node.
 *
 * import_names/import_qns are accepted for signature parity but intentionally
 * NOT fed into the alias map: PASS 1 re-derives aliases from source, and the
 * Elixir IMPORTS-edge QNs are fuzzy (a separate concern — see
 * elixir_stdlib_data.c / the resolver header), so feeding them would inject
 * wrong targets. */
void cbm_run_elixir_lsp_cross(CBMArena *arena, const char *source, int source_len,
                              const char *module_qn, CBMLSPDef *defs, int def_count,
                              const char **import_names, const char **import_qns, int import_count,
                              TSTree *cached_tree, CBMResolvedCallArray *out) {
    (void)import_names;
    (void)import_qns;
    (void)import_count;
    if (!arena || !source || !out)
        return;

    CBMTypeRegistry reg;
    cbm_registry_init(&reg, arena);
    cbm_elixir_stdlib_register(&reg, arena);

    /* Module-identity map: Elixir module name -> def_module_qn (path prefix),
     * recovered from Class defs. Function/Method defs are registered under
     * their real graph QN so lookup_symbol(def_module_qn, "fun/arity") composes
     * back to that QN. */
    CBMHashTable *modmap = cbm_ht_create((uint32_t)(def_count > 0 ? def_count : 1));
    for (int i = 0; i < def_count; i++) {
        const CBMLSPDef *d = &defs[i];
        if (!d->qualified_name || !d->short_name || !d->label)
            continue;
        if (strcmp(d->label, "Class") == 0) {
            if (modmap && d->def_module_qn && d->def_module_qn[0] &&
                !cbm_ht_has(modmap, d->short_name))
                cbm_ht_set(modmap, d->short_name, (void *)d->def_module_qn); /* borrowed */
        } else if (strcmp(d->label, "Function") == 0 || strcmp(d->label, "Method") == 0) {
            CBMRegisteredFunc rf;
            memset(&rf, 0, sizeof(rf));
            rf.qualified_name = d->qualified_name;
            rf.short_name = d->short_name;
            if (strcmp(d->label, "Method") == 0 && d->receiver_type)
                rf.receiver_type = d->receiver_type;
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

    CBMArena idx_arena;
    cbm_arena_init(&idx_arena);
    cbm_registry_finalize_into(&reg, &idx_arena);

    TSTree *tree = cached_tree;
    bool owns_tree = false;
    if (!tree) {
        TSParser *parser = ts_parser_new();
        if (!parser) {
            cbm_ht_free(modmap);
            cbm_arena_destroy(&idx_arena);
            return;
        }
        ts_parser_set_language(parser, tree_sitter_elixir());
        tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
        ts_parser_delete(parser);
        owns_tree = true;
    }
    if (!tree) {
        cbm_ht_free(modmap);
        cbm_arena_destroy(&idx_arena);
        return;
    }

    ElixirLSPContext ctx;
    elixir_lsp_init(&ctx, arena, source, source_len, &reg, module_qn ? module_qn : "", out);
    ctx.cross_module_map = modmap; /* enables the cross-resolution branch */

    elixir_lsp_process_file(&ctx, ts_tree_root_node(tree));

    cbm_ht_free(modmap);
    cbm_arena_destroy(&idx_arena);
    if (owns_tree)
        ts_tree_delete(tree);
}
