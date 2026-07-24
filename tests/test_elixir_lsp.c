/*
 * test_elixir_lsp.c — Tests for the Elixir Light Semantic Pass.
 *
 * The resolver populates result->resolved_calls with CBMResolvedCall edges.
 * Like the Perl pass, def QNs are path-based (module name not woven in), so for
 * these single-file fixtures every def lands under the "test"/"main.ex" module
 * QN; the helpers below use substring matching on the unique callee fragment.
 *
 * PR-1a scaffold: the resolver is a no-op walk, so the only guarantee is that
 * it runs without crashing and emits no edges. PR-1b replaces the placeholder
 * with the qualified/local resolution scenarios (alias expansion, imports,
 * same-module calls, and the zero-edge negatives); PR-1c adds name/arity
 * identity (pipes, captures, default-arg fan-out).
 */
#include "test_framework.h"
#include "cbm.h"
#include "lsp/elixir_lsp.h"
#include <string.h>

/* ── Helpers (mirror test_perl_lsp.c) ──────────────────────────── */

static CBMFileResult *extract_elixir(const char *source) {
    return cbm_extract_file(source, (int)strlen(source), CBM_LANG_ELIXIR, "test", "main.ex", 0,
                            NULL, NULL);
}

static int find_resolved(const CBMFileResult *r, const char *callerFrag, const char *calleeFrag) {
    for (int i = 0; i < r->resolved_calls.count; i++) {
        const CBMResolvedCall *rc = &r->resolved_calls.items[i];
        if (rc->caller_qn && strstr(rc->caller_qn, callerFrag) && rc->callee_qn &&
            strstr(rc->callee_qn, calleeFrag))
            return i;
    }
    return -1;
}

/* Silence unused-function warnings until PR-1b uses the resolution helper. */
static int elixir_lsp_find_resolved_unused(const CBMFileResult *r) {
    return find_resolved(r, "", "");
}

/* ── PR-1a: skeleton runs and emits nothing ────────────────────── */

TEST(elixirlsp_skeleton_runs_no_edges) {
    const char *src = "defmodule Accounts do\n"
                      "  def create(attrs) do\n"
                      "    validate(attrs)\n"
                      "  end\n"
                      "  def validate(attrs), do: attrs\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    /* The resolver ran (no crash). PR-1a emits no resolved calls yet — PR-1b
     * flips this: `create` -> `validate` becomes an lsp_ex_local edge. */
    ASSERT_EQ(0, r->resolved_calls.count);
    (void)elixir_lsp_find_resolved_unused;
    cbm_free_result(r);
    PASS();
}

TEST(elixirlsp_skeleton_empty_module) {
    const char *src = "defmodule Empty do\nend\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT_EQ(0, r->resolved_calls.count);
    cbm_free_result(r);
    PASS();
}

/* ── Suite registration ────────────────────────────────────────── */

SUITE(elixir_lsp) {
    RUN_TEST(elixirlsp_skeleton_runs_no_edges);
    RUN_TEST(elixirlsp_skeleton_empty_module);
}
