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
#include "arena.h"
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

/* ── Cross-file resolution (PR-2b) ─────────────────────────────── */

/* Set up the 4 synthetic project-wide defs for a two-module project:
 *   Mathx  (mathx.ex → module test.mathx):  double/1
 *   Mainx  (mainx.ex → module test.mainx):  run/1  (the caller)
 * The cross resolver is filter-exempt, so both modules' defs are present. */
static void cross_defs(CBMLSPDef defs[4]) {
    memset(defs, 0, 4 * sizeof(CBMLSPDef));
    defs[0].qualified_name = "test.mathx.Mathx";
    defs[0].short_name = "Mathx";
    defs[0].label = "Class";
    defs[0].def_module_qn = "test.mathx";
    defs[1].qualified_name = "test.mathx.double/1";
    defs[1].short_name = "double";
    defs[1].label = "Function";
    defs[1].def_module_qn = "test.mathx";
    defs[2].qualified_name = "test.mainx.Mainx";
    defs[2].short_name = "Mainx";
    defs[2].label = "Class";
    defs[2].def_module_qn = "test.mainx";
    defs[3].qualified_name = "test.mainx.run/1";
    defs[3].short_name = "run";
    defs[3].label = "Function";
    defs[3].def_module_qn = "test.mainx";
}

static const CBMResolvedCall *cross_find(const CBMResolvedCallArray *out, const char *calleeFrag,
                                         const char *strategy) {
    for (int i = 0; i < out->count; i++) {
        const CBMResolvedCall *rc = &out->items[i];
        if (rc->callee_qn && strstr(rc->callee_qn, calleeFrag) &&
            (!strategy || (rc->strategy && strcmp(rc->strategy, strategy) == 0)))
            return rc;
    }
    return NULL;
}

/* A fully-qualified call to another file's module resolves cross-file. */
TEST(elixirlsp_cross_file_basic) {
    const char *src = "defmodule Mainx do\n"
                      "  def run(x), do: Mathx.double(x)\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    cross_defs(defs);
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    const CBMResolvedCall *hit = cross_find(&out, "test.mathx.double/1", "lsp_ex_cross");
    ASSERT(hit != NULL);
    ASSERT(hit->caller_qn && strcmp(hit->caller_qn, "test.mainx.run/1") == 0);
    ASSERT(hit->confidence >= 0.6f);
    cbm_arena_destroy(&arena);
    PASS();
}

/* Cross-file resolution through an alias (`alias Mathx, as: M`). */
TEST(elixirlsp_cross_file_alias) {
    const char *src = "defmodule Mainx do\n"
                      "  alias Mathx, as: M\n"
                      "  def run(x), do: M.double(x)\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    cross_defs(defs);
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    ASSERT(cross_find(&out, "test.mathx.double/1", "lsp_ex_cross") != NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* A call to a module absent from the project defs never resolves to a project
 * def — since Phase 2.6b it CLASSIFIES lsp_ex_external instead (feeding the
 * pipeline suppression; still no graph edge, as external targets have no
 * node). */
TEST(elixirlsp_cross_file_unknown_module) {
    const char *src = "defmodule Mainx do\n"
                      "  def run(x), do: Nowhere.gone(x)\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    cross_defs(defs);
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    ASSERT(cross_find(&out, "Nowhere.gone/1", "lsp_ex_external") != NULL);
    ASSERT(cross_find(&out, "gone", "lsp_ex_cross") == NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* ── use-macro injection + protocol dispatch (PR-2c) ────────────── */

/* A bare call to a function injected by `use Phoenix.Controller`, made inside an
 * action function, resolves lsp_ex_use to the framework function. */
TEST(elixirlsp_use_injected_function) {
    const char *src = "defmodule MyController do\n"
                      "  use Phoenix.Controller\n"
                      "  def index(conn) do\n"
                      "    render(conn, \"index.html\")\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "index", "Phoenix.Controller.render/2", "lsp_ex_use") != NULL);
    cbm_free_result(r);
    PASS();
}

/* Protocol dispatch: a call to `Protocol.fun(x)` resolves cross-file to the
 * protocol's own def (the runtime impl is not statically known). Exercised via
 * the cross resolver with protocol-shaped defs. */
TEST(elixirlsp_protocol_dispatch_cross) {
    const char *src = "defmodule Report do\n"
                      "  def show(x), do: Showcase.Describable.describe(x)\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    memset(defs, 0, sizeof(defs));
    /* The protocol module + its describe/1 def (describable.ex). */
    defs[0].qualified_name = "test.describable.Showcase.Describable";
    defs[0].short_name = "Showcase.Describable";
    defs[0].label = "Class";
    defs[0].def_module_qn = "test.describable";
    defs[1].qualified_name = "test.describable.describe/1";
    defs[1].short_name = "describe";
    defs[1].label = "Function";
    defs[1].def_module_qn = "test.describable";
    /* The caller module. */
    defs[2].qualified_name = "test.report.Report";
    defs[2].short_name = "Report";
    defs[2].label = "Class";
    defs[2].def_module_qn = "test.report";
    defs[3].qualified_name = "test.report.show/1";
    defs[3].short_name = "show";
    defs[3].label = "Function";
    defs[3].def_module_qn = "test.report";
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.report", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    ASSERT(cross_find(&out, "test.describable.describe/1", "lsp_ex_cross") != NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* ── Import-selector resolution (PR-2.5b, ladder rung c) ────────── */

/* `import Enum, only: [map: 2]` then a bare `map(l, f)` classifies stdlib. */
TEST(elixirlsp_import_only_stdlib) {
    const char *src = "defmodule M do\n"
                      "  import Enum, only: [map: 2]\n"
                      "  def run(l), do: map(l, fn x -> x end)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "Enum.map/2", "lsp_ex_stdlib") != NULL);
    cbm_free_result(r);
    PASS();
}

/* An `only:` selector that does not admit the call's arity must not resolve. */
TEST(elixirlsp_import_only_arity_mismatch) {
    const char *src = "defmodule M do\n"
                      "  import Enum, only: [map: 2]\n"
                      "  def run(l), do: map(l)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "map", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* `except:` excludes the listed pair but admits the rest of the module. */
TEST(elixirlsp_import_except) {
    const char *src = "defmodule M do\n"
                      "  import Enum, except: [map: 2]\n"
                      "  def run(l) do\n"
                      "    count(l)\n"
                      "    map(l, fn x -> x end)\n"
                      "  end\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "Enum.count/1", "lsp_ex_stdlib") != NULL);
    ASSERT(find_resolved(r, "run", "Enum.map", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* A bare call imported from a project module resolves cross-file via the
 * module map (lsp_ex_import) — incl. the pipe's +1 arity. */
TEST(elixirlsp_import_cross_file) {
    const char *src = "defmodule Mainx do\n"
                      "  import Mathx, only: [double: 1]\n"
                      "  def run(x), do: x |> double()\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    cross_defs(defs);
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    ASSERT(cross_find(&out, "test.mathx.double/1", "lsp_ex_import") != NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* ── Local captures (PR-2.5a, ladder rung d) ───────────────────── */

/* A local capture `&double/1` passed to Enum.map resolves lsp_ex_capture. */
TEST(elixirlsp_capture_local) {
    const char *src = "defmodule M do\n"
                      "  def double(x), do: x * 2\n"
                      "  def run(list), do: Enum.map(list, &double/1)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "double/1", "lsp_ex_capture") != NULL);
    cbm_free_result(r);
    PASS();
}

/* A capture selects the right arity of a multi-arity function. */
TEST(elixirlsp_capture_arity_selection) {
    const char *src = "defmodule M do\n"
                      "  def f(a), do: a\n"
                      "  def f(a, b), do: a + b\n"
                      "  def run(list), do: Enum.reduce(list, 0, &f/2)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "f/2", "lsp_ex_capture") != NULL);
    ASSERT(find_resolved(r, "run", "f/1", "lsp_ex_capture") == NULL);
    cbm_free_result(r);
    PASS();
}

/* A capture of an unknown function emits no edge (zero-edge guarantee). */
TEST(elixirlsp_capture_unknown_zero_edge) {
    const char *src = "defmodule M do\n"
                      "  def run(list), do: Enum.map(list, &nope/1)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "nope", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* ── Precision pass (PR-2.6a/2.6b) ─────────────────────────────── */

/* In the cross pass (complete module map), a qualified call to a module that
 * is neither in the project nor curated stdlib classifies lsp_ex_external and
 * never binds to a same-named project function. */
TEST(elixirlsp_dep_call_external_cross) {
    const char *src = "defmodule Mainx do\n"
                      "  def run(u), do: HTTPoison.get(u)\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    memset(defs, 0, sizeof(defs));
    defs[0].qualified_name = "test.webx.Webx";
    defs[0].short_name = "Webx";
    defs[0].label = "Class";
    defs[0].def_module_qn = "test.webx";
    defs[1].qualified_name = "test.webx.get/1"; /* same-named project fn */
    defs[1].short_name = "get";
    defs[1].label = "Function";
    defs[1].def_module_qn = "test.webx";
    defs[2].qualified_name = "test.mainx.Mainx";
    defs[2].short_name = "Mainx";
    defs[2].label = "Class";
    defs[2].def_module_qn = "test.mainx";
    defs[3].qualified_name = "test.mainx.run/1";
    defs[3].short_name = "run";
    defs[3].label = "Function";
    defs[3].def_module_qn = "test.mainx";
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    ASSERT(cross_find(&out, "HTTPoison.get/1", "lsp_ex_external") != NULL);
    ASSERT(cross_find(&out, "test.webx.get", NULL) == NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* An Erlang atom-module call is classified lsp_ex_erlang (feeding the
 * pipeline's external-call suppression) and never binds to a same-named
 * project function. */
TEST(elixirlsp_erlang_atom_module) {
    const char *src = "defmodule M do\n"
                      "  def insert(a, b), do: {a, b}\n"
                      "  def run(t, v), do: :ets.insert(t, v)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", ":ets.insert/2", "lsp_ex_erlang") != NULL);
    ASSERT(find_resolved(r, "run", "test.main.insert", NULL) == NULL);
    cbm_free_result(r);
    PASS();
}

/* `import Enum, only: :functions` (category atom, not a name/arity list)
 * admits the module's functions rather than nothing. */
TEST(elixirlsp_import_only_atom_form) {
    const char *src = "defmodule M do\n"
                      "  import Enum, only: :functions\n"
                      "  def run(l), do: map(l, fn x -> x end)\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "Enum.map/2", "lsp_ex_stdlib") != NULL);
    cbm_free_result(r);
    PASS();
}

/* ── Capability pass (PR-2.7a) ─────────────────────────────────── */

/* `alias __MODULE__.Sub` expands against the enclosing module chain. */
TEST(elixirlsp_module_attr_alias) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Sub do\n"
                      "    def go, do: :ok\n"
                      "  end\n"
                      "  alias __MODULE__.Sub\n"
                      "  def run, do: Sub.go()\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "run", "go/0", "lsp_ex_qualified") != NULL);
    cbm_free_result(r);
    PASS();
}

/* defdelegate to a same-file module (with as:) emits lsp_ex_delegate from the
 * delegator to the target. */
TEST(elixirlsp_delegate_samefile) {
    const char *src = "defmodule Outer do\n"
                      "  defmodule Impl do\n"
                      "    def get(k), do: k\n"
                      "  end\n"
                      "  alias Outer.Impl\n"
                      "  defdelegate fetch(k), to: Impl, as: :get\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    const CBMResolvedCall *rc = find_resolved(r, "fetch/1", "get/1", "lsp_ex_delegate");
    ASSERT(rc != NULL);
    cbm_free_result(r);
    PASS();
}

/* defdelegate to a module in another file resolves via the cross map. */
TEST(elixirlsp_delegate_cross) {
    const char *src = "defmodule Mainx do\n"
                      "  defdelegate double(x), to: Mathx\n"
                      "end\n";
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out;
    memset(&out, 0, sizeof(out));
    CBMLSPDef defs[4];
    cross_defs(defs);
    cbm_run_elixir_lsp_cross(&arena, src, (int)strlen(src), "test.mainx", defs, 4, NULL, NULL, 0,
                             NULL, &out);
    const CBMResolvedCall *rc = cross_find(&out, "test.mathx.double/1", "lsp_ex_delegate");
    ASSERT(rc != NULL);
    ASSERT(rc->caller_qn && strstr(rc->caller_qn, "double/1") != NULL);
    cbm_arena_destroy(&arena);
    PASS();
}

/* defdelegate to a curated stdlib module classifies lsp_ex_stdlib. */
TEST(elixirlsp_delegate_stdlib) {
    const char *src = "defmodule M do\n"
                      "  defdelegate fetch(m, k), to: Map\n"
                      "end\n";
    CBMFileResult *r = extract_elixir(src);
    ASSERT(r);
    ASSERT(find_resolved(r, "fetch/2", "Map.fetch/2", "lsp_ex_stdlib") != NULL);
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
    RUN_TEST(elixirlsp_cross_file_basic);
    RUN_TEST(elixirlsp_cross_file_alias);
    RUN_TEST(elixirlsp_cross_file_unknown_module);
    RUN_TEST(elixirlsp_use_injected_function);
    RUN_TEST(elixirlsp_protocol_dispatch_cross);
    RUN_TEST(elixirlsp_capture_local);
    RUN_TEST(elixirlsp_capture_arity_selection);
    RUN_TEST(elixirlsp_capture_unknown_zero_edge);
    RUN_TEST(elixirlsp_erlang_atom_module);
    RUN_TEST(elixirlsp_dep_call_external_cross);
    RUN_TEST(elixirlsp_module_attr_alias);
    RUN_TEST(elixirlsp_delegate_samefile);
    RUN_TEST(elixirlsp_delegate_cross);
    RUN_TEST(elixirlsp_delegate_stdlib);
    RUN_TEST(elixirlsp_import_only_atom_form);
    RUN_TEST(elixirlsp_import_only_stdlib);
    RUN_TEST(elixirlsp_import_only_arity_mismatch);
    RUN_TEST(elixirlsp_import_except);
    RUN_TEST(elixirlsp_import_cross_file);
    RUN_TEST(elixirlsp_empty_module);
}
