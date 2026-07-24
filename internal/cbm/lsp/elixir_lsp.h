#ifndef CBM_LSP_ELIXIR_LSP_H
#define CBM_LSP_ELIXIR_LSP_H

#include "type_rep.h"
#include "scope.h"
#include "type_registry.h"
#include "../cbm.h"
#include "go_lsp.h" /* CBMLSPDef reused across languages */

/* ElixirLSPContext — per-file state for Elixir type-aware call resolution.
 * Mirrors PerlLSPContext / PHPLSPContext / GoLSPContext structure.
 *
 * Elixir differs from Perl in the state it must carry:
 *   - Modules (`defmodule Foo.Bar do`) replace Perl packages. A file may hold
 *     several (nested) modules; current_module_qn tracks the active one.
 *   - `alias`/`import`/`require`/`use` directives (Phase-0 PR-0d extraction)
 *     replace the Exporter `use Foo qw(...)` map. Aliases are line-ordered:
 *     resolution consults only directives declared at lines <= the call site.
 *   - There is no `bless`/`$self` invocant idiom — dispatch is by fully
 *     qualified `Module.fun(...)` calls, pipes (`|>`), and captures (`&M.f/2`),
 *     so resolution centres on the LHS module of a `call`/`dot` node, not on a
 *     blessed receiver variable.
 *
 * QN scheme: like Perl, the structural extractor names each def via
 * cbm_fqn_compute(project, rel_path, name) — the Elixir module name is NOT
 * woven into the Function QN (that is module/Class-node territory, PR-0c/D6).
 * Per-file resolution therefore keys off the file-local registry built from
 * result->defs; module-name-keyed cross-file identity is Phase 2b.
 *
 * Zero-edge guarantee: if a callee cannot be resolved to a registered def, NO
 * edge is emitted (a false edge is worse than a missing one). Dynamic dispatch
 * (`apply/3`, variable modules, `unquote`) resolves to nothing by design. */
typedef struct {
    CBMArena *arena;
    const char *source;
    int source_len;
    const CBMTypeRegistry *registry;
    CBMScope *current_scope;

    /* Module state. A file may declare nested modules; this is the dotted QN of
     * the module currently in effect. Empty string means file/top level. */
    const char *current_module_qn;

    /* alias / require-as map: alias_local[i] is the short name as written in
     * this file (e.g. "Bar" from `alias Foo.Bar`, or the `as:` name);
     * alias_target[i] is the fully-qualified module it expands to. */
    const char **alias_local;
    const char **alias_target;
    int alias_count;
    int alias_cap;

    /* import map: import_module[i] is a module whose functions are imported
     * into local scope (subject to only:/except: selectors, Phase 1b). */
    const char **import_module;
    int import_count;
    int import_cap;

    /* use map: use_module[i] is a `use M` target whose macro expansion is
     * resolved via the curated table (Phase 2c). Recorded here in Phase 1. */
    const char **use_module;
    int use_count;
    int use_cap;

    /* Current context pointers. */
    const char *enclosing_module_qn; /* module QN of the enclosing scope */
    const char *enclosing_func_qn;   /* enclosing def QN, or NULL */
    const char *module_qn;

    /* Output: resolved calls accumulate here. */
    CBMResolvedCallArray *resolved_calls;

    /* Recursion guards (mirror perl_lsp): eval typing depth + AST-walk depth,
     * to bound stack use on pathologically nested input. */
    int eval_depth;
    int walk_depth;

    /* Debug mode (CBM_LSP_DEBUG env). */
    bool debug;
} ElixirLSPContext;

/* Initialize an ElixirLSPContext for processing one file. */
void elixir_lsp_init(ElixirLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                     const CBMTypeRegistry *registry, const char *module_qn,
                     CBMResolvedCallArray *out);

/* Process a file's AST: walk module bodies + def bodies, resolve calls.
 * PR-1a: no-op walk (structure only). PR-1b fills in the resolution ladder. */
void elixir_lsp_process_file(ElixirLSPContext *ctx, TSNode root);

/* Entry point: build registry from file defs + stdlib, then run resolution.
 * Called from cbm_extract_file() via the language dispatch in cbm.c. */
void cbm_run_elixir_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                        TSNode root);

/* Register Elixir stdlib (Kernel auto-imports + curated core modules) into a
 * registry. PR-1a: Kernel stub only; Phase 2a populates the full seed. */
void cbm_elixir_stdlib_register(CBMTypeRegistry *reg, CBMArena *arena);

/* --- Cross-file LSP resolution (Phase 2b) ---
 *
 * Stub-declared here so the fallback-tier wiring (pass_lsp_cross.c, mirroring
 * Kotlin) can be added in Phase 2b without touching this header. Caller
 * supplies the combined CBMLSPDef[] (file-local + cross-file) and a resolved
 * import map (alias/import → target QN). */
void cbm_run_elixir_lsp_cross(CBMArena *arena, const char *source, int source_len,
                              const char *module_qn, CBMLSPDef *defs, int def_count,
                              const char **import_names, const char **import_qns, int import_count,
                              TSTree *cached_tree, /* NULL = parse internally */
                              CBMResolvedCallArray *out);

#endif /* CBM_LSP_ELIXIR_LSP_H */
