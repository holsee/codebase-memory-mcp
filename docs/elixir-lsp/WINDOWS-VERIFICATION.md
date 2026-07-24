# Windows verification runbook — Elixir Hybrid LSP

**Status: DEFERRED, work to be performed.** The Elixir work (Phases 0–2.7) has
been verified on macOS/ARM64 (ASan+UBSan full suite, TSan soak, security
audit). The plan's portability constraint (§2: mingw-w64-clang incl. ARM64
Windows, where ASan is unavailable) has **not** been exercised on a Windows
host. This document is the complete, self-contained checklist for that
verification, deferred by decision at Phase 3 (2026-07-24).

## How to run

1. Build + test per the repo's Windows harness: `scripts/test-windows.ps1`
   (x86-64 mingw-w64-clang first, then ARM64 — no ASan on ARM64; the plain
   runner is the correctness gate there).
2. Run the full suite, then specifically the Elixir surfaces:
   `test-runner elixir_lsp extraction grammar_regression grammar_imports
   grammar_labels lang_contract lsp_resolution_probe`.
3. All `lrp_elixir_s1..s15` probes must be GREEN — S9/S11/S12 are the
   zero-false-edge guarantees, S13/S14/S15 the delegate/behaviour/stdlib-node
   capabilities.

## Elixir-specific items to verify (with known risk points)

| # | Item | Where | Risk / note |
|---|---|---|---|
| 1 | `cbm_setenv`/`cbm_unsetenv` in tests | `test_elixir_lsp.c`, `test_lsp_resolution_probe.c` | Fixed in PR-3a (raw `setenv` does not exist on the Windows CRT). Confirm compile + the S15/opt-in tests behave. |
| 2 | `strtok_r` in multi-alias expansion | `extract_imports.c` (`elixir_expand_multi_alias`), `elixir_lsp.c` | mingw-w64 provides `strtok_r`; MSVC would need `strtok_s`. **Verify link on both arches.** |
| 3 | Suffix gate on `.ex`/`.exs` with Windows paths | `pass_pkgmap.c` (`pkgmap_source_is_elixir`) | Relies on rel-paths being '/'-normalised before the gate (the pipeline normalises; **verify** an indexed Windows-path project still routes Elixir imports through the exact-name strategy). |
| 4 | Synthetic file paths `<elixir-behaviours>` / `<elixir-stdlib>` | injected defs | Never touch the filesystem; verify the store/UI tolerate the angle brackets on Windows (they are plain strings — expected fine). |
| 5 | Deep-nesting stack budget | `elixirlsp_pathological_nesting` (700-deep) | Windows default thread stack is 1 MB (vs 8 MB); the 512-deep walk caps bound recursion — **run the test on ARM64 specifically** (no ASan to catch an overflow). |
| 6 | CRLF sources | selector/attribute text parses (`@behaviour `, `only:`, `to:`) | Byte-prefix parses are CRLF-tolerant by construction; verify with a CRLF-checked-out Elixir fixture. |
| 7 | `getenv` gates | `CBM_ELIXIR_STDLIB_NODES`, `CBM_LSP_DEBUG` | `getenv` is portable; confirm `_putenv_s`-set values are seen (via `cbm_setenv`). |

## Out of scope on Windows

- `docs/elixir-lsp/testing/corpus-metrics.sh` is bash; run the corpus oracle
  via WSL, or port the six sqlite checks to PowerShell if needed.
- TSan (macOS/Linux-only here) — already green on macOS at the tip.

## Sign-off

| Check | x86-64 | ARM64 |
|---|---|---|
| Full suite green | ☐ | ☐ |
| `lrp_elixir_s1..s15` green | ☐ | ☐ |
| Items 1–7 above verified | ☐ | ☐ |

Record the outcome as a dated entry in `PLAN.md` §6 and close the deferral.
