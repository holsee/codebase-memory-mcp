/*
 * test_elixir_lsp.c — Tests for the Elixir Light Semantic Pass.
 *
 * The resolver populates result->resolved_calls with CBMResolvedCall edges.
 * Def QNs are path-based (`<module_qn>.<name>`; the Elixir module name is not
 * woven in), so for these single-file fixtures every def lands under the
 * "test"/"main.ex" module QN and the helpers below match on the unique
 * caller/callee name fragment plus the resolution strategy.
 *
 * Phase 1b covers rungs (a) qualified and (c) local: local calls resolve to
 * file-local defs (lsp_ex_local); qualified `Mod.fun` calls resolve only when
 * Mod (alias-expanded) is a module defined in this file or __MODULE__
 * (lsp_ex_qualified). Cross-file / external-module resolution, and import
 * only:/except: selectors, are Phase 2b/2 — the negative tests pin the
 * zero-edge boundary. Arity identity (pipes, captures, default fan-out) is
 * Phase 1c.
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

static const CBMResolvedCall *find_resolved(const CBMFileResult *r, const char *callerFrag,
                                            const char *calleeFrag, const char *strategy) {
    for (int i = 0; i < r->resolved_calls.count; i++) {
        const CBMResolvedCall *rc = &r->resolved_calls.items[i];
        if (!rc->caller_qn || !rc->callee_qn)
            continue;
        if (!strstr(rc->caller_qn, callerFrag) || !strstr(rc->callee_qn, calleeFrag))
            continue;
        if (strategy && (!rc->strategy || strcmp(rc->strategy, strategy) != 0))
            continue;
        return rc;
    }
    return NULL;
}

static int count_resolved(const CBMFileResult *r, const char *calleeFrag) {
    int n = 0;
    for (int i = 0; i < r->resolved_calls.count; i++) {
        const CBMResolvedCall *rc = &r->resolved_calls.items[i];
        if (rc->callee_qn && strstr(rc->callee_qn, calleeFrag))
            n++;
    }
    return n;
}

/* ── (c) Local same-module resolution ──────────────────────────── */

TEST(elixirlsp_local_same_module_call) {
    const char *src = "defmodule Accounts do\n"
                      "  def create(attrs) do\n"
                      "    validate(attrs)\n"
                      "  end\n"
                      "  def validate(attrs), do: attrs\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "create", "validate", "lsp_ex_local") != NULL);
    cbm_free_result(r);
    PASS();
}

/* ── (a) Qualified resolution via alias to a same-file module ──── */

TEST(elixirlsp_qualified_via_alias) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Inner do\n"
                      "    def helper(x), do: x\n"
                      "  end\n"
                      "  alias Outer.Inner\n"
                      "  def run(x) do\n"
                      "    Inner.helper(x)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "helper", "lsp_ex_qualified") != NULL);
    cbm_free_result(r);
    PASS();
}

TEST(elixirlsp_qualified_as_alias) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Inner do\n"
                      "    def helper(x), do: x\n"
                      "  end\n"
                      "  alias Outer.Inner, as: Helper\n"
                      "  def run(x) do\n"
                      "    Helper.helper(x)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "helper", "lsp_ex_qualified") != NULL);
    cbm_free_result(r);
    PASS();
}

TEST(elixirlsp_qualified_multi_alias) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Bar do\n"
                      "    def go(x), do: x\n"
                      "  end\n"
                      "  defmodule Baz do\n"
                      "    def stop(x), do: x\n"
                      "  end\n"
                      "  alias Outer.{Bar, Baz}\n"
                      "  def run(x) do\n"
                      "    Bar.go(x)\n"
                      "    Baz.stop(x)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "go", "lsp_ex_qualified") != NULL);
    ASSERT(find_resolved(r, "run", "stop", "lsp_ex_qualified") != NULL);
    cbm_free_result(r);
    PASS();
}

TEST(elixirlsp_qualified_transitive_alias) {
    const char *src = "defmodule App do\n"
                      "  defmodule Core do\n"
                      "    defmodule Impl do\n"
                      "      def go, do: :ok\n"
                      "    end\n"
                      "  end\n"
                      "  alias App.Core\n"
                      "  alias Core.Impl\n"
                      "  def run do\n"
                      "    Impl.go()\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "go", "lsp_ex_qualified") != NULL);
    cbm_free_result(r);
    PASS();
}

/* Alias line-visibility: a qualified call ABOVE the alias does not resolve
 * (the bare module name is not a defined module); the call BELOW it does. */
TEST(elixirlsp_alias_line_visibility) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Helpers do\n"
                      "    def h, do: :ok\n"
                      "  end\n"
                      "  def early, do: Helpers.h()\n"
                      "  alias Outer.Helpers\n"
                      "  def late, do: Helpers.h()\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "late", "h", "lsp_ex_qualified") != NULL);
    ASSERT(find_resolved(r, "early", "h", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* ── Zero-edge guarantees ──────────────────────────────────────── */

/* An external module (not defined in this file) does not resolve, even when a
 * same-named function exists locally — the qualified gate blocks the false
 * cross-module edge. */
TEST(elixirlsp_external_module_no_edge) {
    const char *src = "defmodule M do\n"
                      "  def upcase(s), do: s\n"
                      "  def run(s) do\n"
                      "    String.upcase(s)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    /* No qualified edge to the local upcase from String.upcase. */
    ASSERT(find_resolved(r, "run", "upcase", "lsp_ex_qualified") == NULL);
    cbm_free_result(r);
    PASS();
}

/* Dynamic dispatch on a variable module and apply/3 emit no edge. */
TEST(elixirlsp_dynamic_dispatch_no_edge) {
    const char *src = "defmodule M do\n"
                      "  def handler(x), do: x\n"
                      "  def run(mod, x) do\n"
                      "    mod.handler(x)\n"
                      "    apply(mod, :handler, [x])\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT_EQ(0, count_resolved(r, "handler"));
    cbm_free_result(r);
    PASS();
}

/* An import directive is parsed without breaking local resolution. */
TEST(elixirlsp_import_does_not_break_resolution) {
    const char *src = "defmodule M do\n"
                      "  import Enum\n"
                      "  def run(x) do\n"
                      "    helper(x)\n"
                      "  end\n"
                      "  def helper(x), do: x\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "helper", "lsp_ex_local") != NULL);
    cbm_free_result(r);
    PASS();
}

/* ── Name/arity identity (D3, Phase 1c) ────────────────────────── */

/* foo/1 and foo/2 are distinct nodes; a call resolves to the matching arity. */
TEST(elixirlsp_arity_disambiguation) {
    const char *src = "defmodule M do\n"
                      "  def foo(a), do: a\n"
                      "  def foo(a, b), do: a + b\n"
                      "  def run(x) do\n"
                      "    foo(x)\n"
                      "    foo(x, x)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "foo/1", "lsp_ex_local") != NULL);
    ASSERT(find_resolved(r, "run", "foo/2", "lsp_ex_local") != NULL);
    cbm_free_result(r);
    PASS();
}

/* A pipe adds one argument: `x |> add(1)` resolves add/2, not add/1. */
TEST(elixirlsp_pipe_arity) {
    const char *src = "defmodule M do\n"
                      "  def add(a, b), do: a + b\n"
                      "  def run(x), do: x |> add(1)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "add/2", "lsp_ex_local") != NULL);
    ASSERT(find_resolved(r, "run", "add/1", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* A default arg fans out: `def greet(name, greeting \\ "hi")` defines greet/1
 * and greet/2, so both a 1-arg and a 2-arg call resolve. */
TEST(elixirlsp_default_arg_fanout) {
    const char *src = "defmodule M do\n"
                      "  def greet(name, greeting \\\\ \"hi\"), do: greeting <> name\n"
                      "  def run() do\n"
                      "    greet(\"a\")\n"
                      "    greet(\"a\", \"yo\")\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "greet/1", "lsp_ex_local") != NULL);
    ASSERT(find_resolved(r, "run", "greet/2", "lsp_ex_local") != NULL);
    cbm_free_result(r);
    PASS();
}

/* ── Curated stdlib (PR-2a) ─────────────────────────────────────── */

/* A bare call that is not a file-local def resolves to the Kernel auto-import. */
TEST(elixirlsp_kernel_builtin) {
    const char *src = "defmodule M do\n"
                      "  def run(x) do\n"
                      "    length(x)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "Kernel.length/1", "lsp_ex_kernel") != NULL);
    cbm_free_result(r);
    PASS();
}

/* A qualified call to a curated core module classifies lsp_ex_stdlib. */
TEST(elixirlsp_stdlib_qualified) {
    const char *src = "defmodule M do\n"
                      "  def run(l) do\n"
                      "    Enum.map(l, fn x -> x end)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "Enum.map/2", "lsp_ex_stdlib") != NULL);
    cbm_free_result(r);
    PASS();
}

/* Stdlib resolution is arity-precise: GenServer.call/2 vs /3. */
TEST(elixirlsp_stdlib_arity) {
    const char *src = "defmodule M do\n"
                      "  def ask(pid), do: GenServer.call(pid, :x)\n"
                      "  def ask3(pid), do: GenServer.call(pid, :x, 5000)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "ask", "GenServer.call/2", "lsp_ex_stdlib") != NULL);
    ASSERT(find_resolved(r, "ask3", "GenServer.call/3", "lsp_ex_stdlib") != NULL);
    cbm_free_result(r);
    PASS();
}

/* A resolver run over an empty module emits nothing and does not crash. */
TEST(elixirlsp_empty_module) {
    const char *src = "defmodule Empty do\nend\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT_EQ(0, r->resolved_calls.count);
    cbm_free_result(r);
    PASS();
}

/* ── Suite registration ────────────────────────────────────────── */

SUITE(elixir_lsp) {
    RUN_TEST(elixirlsp_local_same_module_call);
    RUN_TEST(elixirlsp_qualified_via_alias);
    RUN_TEST(elixirlsp_qualified_as_alias);
    RUN_TEST(elixirlsp_qualified_multi_alias);
    RUN_TEST(elixirlsp_qualified_transitive_alias);
    RUN_TEST(elixirlsp_alias_line_visibility);
    RUN_TEST(elixirlsp_external_module_no_edge);
    RUN_TEST(elixirlsp_dynamic_dispatch_no_edge);
    RUN_TEST(elixirlsp_import_does_not_break_resolution);
    RUN_TEST(elixirlsp_arity_disambiguation);
    RUN_TEST(elixirlsp_pipe_arity);
    RUN_TEST(elixirlsp_default_arg_fanout);
    RUN_TEST(elixirlsp_kernel_builtin);
    RUN_TEST(elixirlsp_stdlib_qualified);
    RUN_TEST(elixirlsp_stdlib_arity);
    RUN_TEST(elixirlsp_empty_module);
}
