/*
 * elixir_stdlib_data.c — hand-written Elixir stdlib seed (curated knowledge).
 *
 * Registers Elixir core functions into the resolver's CBMTypeRegistry at
 * name/arity granularity so the resolver can recognise a call as a stdlib call:
 *   - Kernel auto-imports (is_atom/1, elem/2, length/1, …) — reachable BARE
 *     from any module; resolved by the local rung's Kernel fallback
 *     (lsp_ex_kernel).
 *   - Curated core modules (Enum, Map, String, List, Keyword, Process,
 *     GenServer, Supervisor, Task, Agent) — resolved by the qualified rung
 *     (lsp_ex_stdlib).
 *
 * Entries are keyed to match the resolver's arity-suffixed lookup
 * (cbm_registry_lookup_symbol(module, "fun/arity") composes "module.fun/arity"):
 * qualified_name = "Enum.map/2", short_name = "map/2".
 *
 * Scope note — this seed is KNOWLEDGE, not graph nodes. A resolved stdlib call
 * carries the lsp_ex_kernel/lsp_ex_stdlib strategy in result->resolved_calls,
 * but a CALLS *edge* only forms when the callee QN matches an existing graph
 * node (src/pipeline/lsp_resolve.h), and stdlib modules are not indexed. So
 * stdlib resolution classifies the call (and is the foundation for the use-macro
 * table in PR-2c); minting stdlib nodes for edges (à la kotlin_builtins.c) is a
 * deliberately separate, opt-in choice (it inflates every project's node count).
 * Return types are UNKNOWN — arity-correct classification is the goal, matching
 * the Perl precedent.
 */

#include "../type_rep.h"
#include "../type_registry.h"
#include "../../arena.h"
#include "../elixir_lsp.h"
#include <string.h>

/* One curated stdlib entry: module, function, arity. */
typedef struct {
    const char *module;
    const char *fun;
    int arity;
} ElixirStdEntry;

static const ElixirStdEntry kElixirStdlib[] = {
    /* ── Kernel (auto-imported; called bare) ───────────────────────── */
    {"Kernel", "is_atom", 1},
    {"Kernel", "is_binary", 1},
    {"Kernel", "is_bitstring", 1},
    {"Kernel", "is_boolean", 1},
    {"Kernel", "is_float", 1},
    {"Kernel", "is_function", 1},
    {"Kernel", "is_function", 2},
    {"Kernel", "is_integer", 1},
    {"Kernel", "is_list", 1},
    {"Kernel", "is_map", 1},
    {"Kernel", "is_map_key", 2},
    {"Kernel", "is_nil", 1},
    {"Kernel", "is_number", 1},
    {"Kernel", "is_pid", 1},
    {"Kernel", "is_port", 1},
    {"Kernel", "is_reference", 1},
    {"Kernel", "is_tuple", 1},
    {"Kernel", "elem", 2},
    {"Kernel", "put_elem", 3},
    {"Kernel", "hd", 1},
    {"Kernel", "tl", 1},
    {"Kernel", "length", 1},
    {"Kernel", "map_size", 1},
    {"Kernel", "tuple_size", 1},
    {"Kernel", "byte_size", 1},
    {"Kernel", "bit_size", 1},
    {"Kernel", "abs", 1},
    {"Kernel", "ceil", 1},
    {"Kernel", "floor", 1},
    {"Kernel", "round", 1},
    {"Kernel", "trunc", 1},
    {"Kernel", "div", 2},
    {"Kernel", "rem", 2},
    {"Kernel", "max", 2},
    {"Kernel", "min", 2},
    {"Kernel", "to_string", 1},
    {"Kernel", "to_charlist", 1},
    {"Kernel", "inspect", 1},
    {"Kernel", "inspect", 2},
    {"Kernel", "send", 2},
    {"Kernel", "self", 0},
    {"Kernel", "spawn", 1},
    {"Kernel", "spawn", 3},
    {"Kernel", "spawn_link", 1},
    {"Kernel", "make_ref", 0},
    {"Kernel", "raise", 1},
    {"Kernel", "raise", 2},
    {"Kernel", "throw", 1},
    {"Kernel", "exit", 1},
    {"Kernel", "then", 2},
    {"Kernel", "tap", 2},
    {"Kernel", "get_in", 2},
    {"Kernel", "put_in", 3},
    {"Kernel", "update_in", 3},
    {"Kernel", "struct", 2},
    {"Kernel", "struct!", 2},
    {"Kernel", "function_exported?", 3},

    /* ── Enum ──────────────────────────────────────────────────────── */
    {"Enum", "map", 2},
    {"Enum", "each", 2},
    {"Enum", "reduce", 2},
    {"Enum", "reduce", 3},
    {"Enum", "filter", 2},
    {"Enum", "reject", 2},
    {"Enum", "find", 2},
    {"Enum", "find", 3},
    {"Enum", "count", 1},
    {"Enum", "count", 2},
    {"Enum", "member?", 2},
    {"Enum", "at", 2},
    {"Enum", "sum", 1},
    {"Enum", "all?", 1},
    {"Enum", "all?", 2},
    {"Enum", "any?", 1},
    {"Enum", "any?", 2},
    {"Enum", "into", 2},
    {"Enum", "sort", 1},
    {"Enum", "sort", 2},
    {"Enum", "sort_by", 2},
    {"Enum", "uniq", 1},
    {"Enum", "uniq_by", 2},
    {"Enum", "join", 1},
    {"Enum", "join", 2},
    {"Enum", "flat_map", 2},
    {"Enum", "group_by", 2},
    {"Enum", "take", 2},
    {"Enum", "drop", 2},
    {"Enum", "chunk_every", 2},
    {"Enum", "with_index", 1},
    {"Enum", "map_join", 3},
    {"Enum", "frequencies", 1},
    {"Enum", "zip", 2},
    {"Enum", "empty?", 1},
    {"Enum", "to_list", 1},
    {"Enum", "min_by", 2},
    {"Enum", "max_by", 2},

    /* ── Map ───────────────────────────────────────────────────────── */
    {"Map", "get", 2},
    {"Map", "get", 3},
    {"Map", "put", 3},
    {"Map", "delete", 2},
    {"Map", "keys", 1},
    {"Map", "values", 1},
    {"Map", "merge", 2},
    {"Map", "has_key?", 2},
    {"Map", "fetch", 2},
    {"Map", "fetch!", 2},
    {"Map", "update", 4},
    {"Map", "put_new", 3},
    {"Map", "take", 2},
    {"Map", "drop", 2},
    {"Map", "new", 0},
    {"Map", "new", 1},
    {"Map", "to_list", 1},
    {"Map", "from_struct", 1},

    /* ── String ────────────────────────────────────────────────────── */
    {"String", "split", 1},
    {"String", "split", 2},
    {"String", "trim", 1},
    {"String", "trim", 2},
    {"String", "upcase", 1},
    {"String", "downcase", 1},
    {"String", "replace", 3},
    {"String", "contains?", 2},
    {"String", "length", 1},
    {"String", "slice", 2},
    {"String", "slice", 3},
    {"String", "to_integer", 1},
    {"String", "to_atom", 1},
    {"String", "starts_with?", 2},
    {"String", "ends_with?", 2},
    {"String", "capitalize", 1},
    {"String", "duplicate", 2},
    {"String", "reverse", 1},
    {"String", "trim_trailing", 1},

    /* ── List ──────────────────────────────────────────────────────── */
    {"List", "first", 1},
    {"List", "last", 1},
    {"List", "flatten", 1},
    {"List", "delete", 2},
    {"List", "insert_at", 3},
    {"List", "keyfind", 3},
    {"List", "wrap", 1},
    {"List", "duplicate", 2},
    {"List", "to_tuple", 1},
    {"List", "foldl", 3},
    {"List", "foldr", 3},

    /* ── Keyword ───────────────────────────────────────────────────── */
    {"Keyword", "get", 2},
    {"Keyword", "get", 3},
    {"Keyword", "put", 3},
    {"Keyword", "has_key?", 2},
    {"Keyword", "fetch", 2},
    {"Keyword", "keys", 1},
    {"Keyword", "values", 1},
    {"Keyword", "merge", 2},
    {"Keyword", "delete", 2},

    /* ── Process ───────────────────────────────────────────────────── */
    {"Process", "send", 3},
    {"Process", "alive?", 1},
    {"Process", "exit", 2},
    {"Process", "register", 2},
    {"Process", "whereis", 1},
    {"Process", "monitor", 1},
    {"Process", "sleep", 1},
    {"Process", "put", 2},
    {"Process", "get", 1},
    {"Process", "link", 1},

    /* ── GenServer ─────────────────────────────────────────────────── */
    {"GenServer", "start_link", 2},
    {"GenServer", "start_link", 3},
    {"GenServer", "call", 2},
    {"GenServer", "call", 3},
    {"GenServer", "cast", 2},
    {"GenServer", "reply", 2},
    {"GenServer", "stop", 1},
    {"GenServer", "stop", 3},

    /* ── Supervisor ────────────────────────────────────────────────── */
    {"Supervisor", "start_link", 2},
    {"Supervisor", "start_link", 3},
    {"Supervisor", "start_child", 2},
    {"Supervisor", "which_children", 1},

    /* ── Task ──────────────────────────────────────────────────────── */
    {"Task", "start", 1},
    {"Task", "start_link", 1},
    {"Task", "async", 1},
    {"Task", "async", 3},
    {"Task", "await", 1},
    {"Task", "await", 2},
    {"Task", "await_many", 1},

    /* ── Agent ─────────────────────────────────────────────────────── */
    {"Agent", "start_link", 1},
    {"Agent", "get", 2},
    {"Agent", "get", 3},
    {"Agent", "update", 2},
    {"Agent", "cast", 2},
    {"Agent", "stop", 1},

    /* ── use-injected framework functions (Phase 2c) ────────────────────
     * Functions a `use Framework` macro injects into the module scope, so a
     * bare call resolves via the local rung's use-table pass (lsp_ex_use),
     * keyed on the framework module: lookup_symbol("Phoenix.Controller",
     * "render/2"). */

    /* use Phoenix.Controller */
    {"Phoenix.Controller", "render", 2},
    {"Phoenix.Controller", "render", 3},
    {"Phoenix.Controller", "json", 2},
    {"Phoenix.Controller", "text", 2},
    {"Phoenix.Controller", "html", 2},
    {"Phoenix.Controller", "redirect", 2},
    {"Phoenix.Controller", "put_flash", 3},
    {"Phoenix.Controller", "put_status", 2},
    {"Phoenix.Controller", "put_view", 2},
    {"Phoenix.Controller", "action_name", 1},

    /* use Phoenix.LiveView / Phoenix.Component */
    {"Phoenix.LiveView", "assign", 2},
    {"Phoenix.LiveView", "assign", 3},
    {"Phoenix.LiveView", "assign_new", 3},
    {"Phoenix.LiveView", "push_navigate", 2},
    {"Phoenix.LiveView", "push_patch", 2},
    {"Phoenix.LiveView", "send_update", 2},
    {"Phoenix.LiveView", "connected?", 1},
    {"Phoenix.LiveView", "allow_upload", 3},
    {"Phoenix.Component", "assign", 2},
    {"Phoenix.Component", "assign", 3},

    /* use Ecto.Schema */
    {"Ecto.Schema", "field", 2},
    {"Ecto.Schema", "field", 3},
    {"Ecto.Schema", "belongs_to", 2},
    {"Ecto.Schema", "belongs_to", 3},
    {"Ecto.Schema", "has_many", 2},
    {"Ecto.Schema", "has_many", 3},
    {"Ecto.Schema", "has_one", 2},
    {"Ecto.Schema", "has_one", 3},
    {"Ecto.Schema", "many_to_many", 3},
    {"Ecto.Schema", "embeds_one", 2},
    {"Ecto.Schema", "embeds_one", 3},
    {"Ecto.Schema", "embeds_many", 2},
    {"Ecto.Schema", "embeds_many", 3},
    {"Ecto.Schema", "timestamps", 0},
    {"Ecto.Schema", "timestamps", 1},
    {"Ecto.Schema", "schema", 2},

    /* use ExUnit.Case (test/describe/setup + common assertions) */
    {"ExUnit.Case", "test", 2},
    {"ExUnit.Case", "test", 3},
    {"ExUnit.Case", "describe", 2},
    {"ExUnit.Case", "setup", 1},
    {"ExUnit.Case", "setup_all", 1},
    {"ExUnit.Case", "assert", 1},
    {"ExUnit.Case", "assert", 2},
    {"ExUnit.Case", "refute", 1},
    {"ExUnit.Case", "assert_receive", 1},
    {"ExUnit.Case", "assert_received", 1},

    /* use GenServer / Supervisor — the callable the macro injects (callbacks
     * like handle_call are user-defined defs, resolved elsewhere). */
    {"GenServer", "child_spec", 1},
    {"Supervisor", "child_spec", 1},
};

void cbm_elixir_stdlib_register(CBMTypeRegistry *reg, CBMArena *arena) {
    const CBMType **rets = (const CBMType **)cbm_arena_alloc(arena, 2 * sizeof(*rets));
    if (rets) {
        rets[0] = cbm_type_unknown();
        rets[1] = NULL;
    }
    const int n = (int)(sizeof(kElixirStdlib) / sizeof(kElixirStdlib[0]));
    for (int i = 0; i < n; i++) {
        const ElixirStdEntry *e = &kElixirStdlib[i];
        CBMRegisteredFunc rf;
        memset(&rf, 0, sizeof(rf));
        rf.min_params = -1;
        /* QN "Mod.fun/arity", short "fun/arity" — matches the resolver's
         * cbm_registry_lookup_symbol(module, "fun/arity") composition. */
        rf.qualified_name = cbm_arena_sprintf(arena, "%s.%s/%d", e->module, e->fun, e->arity);
        rf.short_name = cbm_arena_sprintf(arena, "%s/%d", e->fun, e->arity);
        rf.signature = cbm_type_func(arena, NULL, NULL, rets);
        cbm_registry_add_func(reg, rf);
    }
}

/* Opt-in stdlib node injection (Phase 2.7c). Behind CBM_ELIXIR_STDLIB_NODES
 * (checked by the caller): inject every curated entry as a graph node — a
 * Class per module and a Function per entry — with QNs identical to the
 * resolver's emitted callee_qns ("Enum.map/2"), so lsp_ex_stdlib /
 * lsp_ex_kernel / lsp_ex_use classifications resolve to real nodes and form
 * CALLS edges. Default OFF: injection inflates every project's node count, so
 * it is a deliberate opt-in (mirrors kotlin_builtins.c, which injects a tiny
 * set unconditionally). Upsert-by-QN dedups across files. */
void cbm_elixir_stdlib_inject_defs(CBMFileResult *result, CBMArena *arena) {
    if (!result || !arena) {
        return;
    }
    const int n = (int)(sizeof(kElixirStdlib) / sizeof(kElixirStdlib[0]));
    const char *seen[64];
    int nseen = 0;
    for (int i = 0; i < n; i++) {
        const ElixirStdEntry *e = &kElixirStdlib[i];
        bool have = false;
        for (int k = 0; k < nseen; k++) {
            if (strcmp(seen[k], e->module) == 0) {
                have = true;
                break;
            }
        }
        if (!have && nseen < 64) {
            seen[nseen++] = e->module;
            CBMDefinition md;
            memset(&md, 0, sizeof(md));
            md.name = e->module;
            md.qualified_name = e->module;
            md.label = "Class";
            md.file_path = "<elixir-stdlib>";
            md.start_line = 1;
            md.end_line = 1;
            md.is_exported = true;
            cbm_defs_push(&result->defs, arena, md);
        }
        CBMDefinition fd;
        memset(&fd, 0, sizeof(fd));
        fd.name = e->fun;
        fd.qualified_name = cbm_arena_sprintf(arena, "%s.%s/%d", e->module, e->fun, e->arity);
        fd.label = "Function";
        fd.file_path = "<elixir-stdlib>";
        fd.start_line = 1;
        fd.end_line = 1;
        fd.is_exported = true;
        cbm_defs_push(&result->defs, arena, fd);
    }
}
