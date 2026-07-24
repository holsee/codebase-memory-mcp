# Frozen evaluation question set — Elixir Hybrid LSP

Frozen 2026-07-23, before checkpoint B. Do not edit questions or keys after
baseline; additions require a new versioned section and re-baselining.

**Corpus (pinned):**

| Repo | SHA | Files (.ex/.exs) |
|---|---|---|
| plausible/analytics | `91fb504aa63f9d1289541230944af1541a73117d` | 1210 |
| elixir-plug/plug | `2463704245eccacb2c528d7651cf86120b9f0543` | 78 |
| phoenixframework/phoenix | `bb81d88844302c65a97473459d6ecfca3e685af8` | 177 |

**Runner:** `claude -p --model opus --mcp-config <variant>.json --allowedTools
"mcp__cbm__*"`, question text appended with: *"Answer using only the MCP graph
tools. Cite the tool calls you made."* Up to 3 attempts; best grade counts.
Grade PASS (1.0) / PARTIAL (0.5) / FAIL (0.0) blind against the keys below.

## Standard rubric (Q1–Q12) — repo: `plug`

Per `docs/BENCHMARK.md` methodology. Concrete phrasing and keys:

| # | Question | PASS key | PARTIAL key |
|---|---|---|---|
| Q1 | "How many nodes and edges are in the graph for this project, and what node labels exist?" | Reports node+edge counts and ≥5 labels via get_graph_schema | Counts only, no labels |
| Q2 | "Find the most-called functions in this codebase." | ≥3 real Plug functions with in-degree evidence | Functions listed, no degree evidence |
| Q3 | "List the main modules/classes." | ≥3 of Plug.Conn, Plug.Router, Plug.Builder, Plug.Static etc. | Fewer or file-level only |
| Q4 | "Find all functions whose name contains `resp`." | ≥3 of put_resp_header/put_resp_content_type/send_resp/resp/merge_resp_headers | 1–2 matches |
| Q5 | "Show me the source code of `Plug.Conn.halt`." | Returns the actual `def halt(%Conn{} = conn)` body (lib/plug/conn.ex ~:1900) | Wrong overload/partial source |
| Q6 | "Search the code for uses of `List.keystore`." | Finds lib/plug/conn.ex occurrence(s) with function context | Raw match, no context |
| Q7 | "What does `Plug.Conn.send_resp` call?" (outbound) | ≥2 real callees with correct module attribution | 1 callee or module-level only attribution |
| Q8 | "Who calls `Plug.Conn.halt`?" (inbound) | ≥1 real caller identified as a *function* (not just a module/file) | Caller found at module/file granularity only |
| Q9 | "Run a Cypher query returning 5 CALLS edges between functions." | 5 rows, both endpoints Function-labelled | Rows returned, endpoints module-level |
| Q10 | "What are the parameters/signature of `Plug.Conn.put_resp_header`?" | Signature with conn/key/value (3 params) | Name only, params missing/null |
| Q11 | "Which modules use/inherit behaviour `Plug` (implement `call/2` + `init/1`)?" | ≥2 plug-behaviour modules via graph query | Text-search-only answer |
| Q12 | "List the top-level files and directories of the project." | lib/, test/, mix.exs present | Partial listing |

## Elixir-specific (E1–E5)

| # | Repo | Question | PASS key | PARTIAL key |
|---|---|---|---|---|
| E1 | plug | "Which functions does `Plug.Conn.put_resp_header/3` call, and who calls it within this project?" | Callees include ≥2 of validate_header_key_normalized_if_test!/validate_header_key_value!/List.keystore (guarded multi-clause def must be found at all — D1); callers include ≥2 of Plug.BasicAuth, Plug.Debugger, Plug.Conn.update_resp_header, Plug.Conn.put_resp_content_type | Def found + one correct side |
| E2 | plug | "In the pipeline `conn \|> put_status(404) \|> halt()`, which module and function does each stage resolve to, at what arity?" | Both resolve to Plug.Conn: put_status/2 and halt/1 (pipe adds the piped arg) | Modules right, arity wrong/missing |
| E3 | analytics | "List the GenServer callbacks implemented in `Plausible.Ingestion.WriteBuffer`." | ≥4 of init/1, handle_cast/2, handle_info/2, handle_call/3, terminate/2 identified as functions of that module | Some callbacks, wrong module attribution |
| E4 | phoenix | "Which types/modules implement the protocol `Phoenix.Param`?" | ≥4 of Integer, Float, BitString, Atom, Map, Any (lib/phoenix/param.ex; Date appears only in a doc example) | Protocol found, impls missing (defimpl not extracted — D4) |
| E5 | analytics | "Where is the route `POST /api/event` handled? Name the module and function." | PlausibleWeb router → `Api.ExternalController.event` (router.ex ~:396) | Router file found, handler not resolved |

## Grading notes

- Q7/Q8/E1/E3 are the **enclosing-function-attribution** probes (D2): at
  baseline, expect call endpoints at module granularity — grade honestly, the
  delta is the point.
- E1 doubles as the **guard-clause def** probe (D1): if
  `put_resp_header/3` has no Function node at baseline, FAIL is expected.
- E4 probes `defimpl` extraction (D4); E5 is a regression guard for the
  already-working Phoenix router service pattern.
- Record for every question: grade, attempts, tools used, one-line failure
  reason. The grader must not know which binary variant produced the answer.
