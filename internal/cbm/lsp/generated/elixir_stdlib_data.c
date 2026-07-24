/*
 * elixir_stdlib_data.c — hand-written Elixir stdlib seed.
 *
 * Strategy mirrors perl_stdlib_data.c:
 *   1. Kernel auto-imports (is_atom, elem, hd, length, ...) registered as
 *      global, package-less functions reachable from any module without an
 *      explicit import — Elixir auto-imports Kernel into every module.
 *   2. (Phase 2a) curated core modules (Enum, Map, String, List, Keyword,
 *      Process, GenServer, Supervisor, Task, Agent) as module-qualified
 *      functions, so `Enum.map`, `GenServer.call`, etc. resolve.
 *
 * PR-1a ships the Kernel stub only — a baseline symbol table for the local
 * resolution rung (PR-1b). Return types are UNKNOWN: arity-correct resolution
 * is the goal (matching the Perl precedent), not type inference. The full
 * curated seed lands in Phase 2a (PR-2a).
 */

#include "../type_rep.h"
#include "../type_registry.h"
#include "../../arena.h"
#include "../elixir_lsp.h"
#include <string.h>

#define MIXED cbm_type_unknown()

/* Register a global (auto-imported) Kernel function returning `ret_type_`.
 * Reachable from any module — short_name == qualified_name (bare name). */
#define REG_KERNEL(name_, ret_type_)                                                            \
    do {                                                                                        \
        memset(&rf, 0, sizeof(rf));                                                             \
        rf.min_params = -1;                                                                     \
        rf.qualified_name = (name_);                                                            \
        rf.short_name = (name_);                                                                \
        {                                                                                       \
            const CBMType **rets = (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(*rets)); \
            rets[0] = (ret_type_);                                                              \
            rets[1] = NULL;                                                                     \
            rf.signature = cbm_type_func(arena, NULL, NULL, rets);                              \
        }                                                                                       \
        cbm_registry_add_func(reg, rf);                                                         \
    } while (0)

void cbm_elixir_stdlib_register(CBMTypeRegistry *reg, CBMArena *arena) {
    CBMRegisteredFunc rf;

    /* ── Kernel auto-imports (global, package-less) ─────────────────
     * A representative subset of the Kernel functions/guards auto-imported
     * into every module. Return types unknown for v1; the full list and the
     * curated Enum/Map/String/... modules land in PR-2a. */

    /* Type-check guards. */
    REG_KERNEL("is_atom", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_binary", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_bitstring", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_boolean", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_float", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_function", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_integer", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_list", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_map", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_nil", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_number", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_pid", cbm_type_builtin(arena, "bool"));
    REG_KERNEL("is_tuple", cbm_type_builtin(arena, "bool"));

    /* Data access / inspection. */
    REG_KERNEL("elem", MIXED);
    REG_KERNEL("hd", MIXED);
    REG_KERNEL("tl", MIXED);
    REG_KERNEL("length", cbm_type_builtin(arena, "int"));
    REG_KERNEL("map_size", cbm_type_builtin(arena, "int"));
    REG_KERNEL("tuple_size", cbm_type_builtin(arena, "int"));
    REG_KERNEL("abs", MIXED);
    REG_KERNEL("max", MIXED);
    REG_KERNEL("min", MIXED);
    REG_KERNEL("round", cbm_type_builtin(arena, "int"));
    REG_KERNEL("trunc", cbm_type_builtin(arena, "int"));

    /* Conversion / formatting. */
    REG_KERNEL("to_string", cbm_type_builtin(arena, "string"));
    REG_KERNEL("inspect", cbm_type_builtin(arena, "string"));

    /* Control / process primitives. */
    REG_KERNEL("raise", MIXED);
    REG_KERNEL("throw", MIXED);
    REG_KERNEL("send", MIXED);
    REG_KERNEL("spawn", MIXED);
    REG_KERNEL("then", MIXED);
    REG_KERNEL("tap", MIXED);
}
