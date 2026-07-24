#!/usr/bin/env bash
# corpus-metrics.sh — before/after graph facts for the pinned elixir_showcase
# corpus. Indexes the corpus with a given binary into an isolated cache and
# prints the capability checks used as the Phase-0 before/after oracle.
#
# Usage: corpus-metrics.sh <cbm-binary> <cache-dir>
# Compare two runs (e.g. pre-Phase-0 binary vs Phase-0 binary) side by side.
set -uo pipefail

BIN="${1:?usage: corpus-metrics.sh <cbm-binary> <cache-dir>}"
CACHE="${2:?usage: corpus-metrics.sh <cbm-binary> <cache-dir>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPUS="$HERE/corpus/elixir_showcase"

rm -rf "$CACHE"; mkdir -p "$CACHE"
CBM_CACHE_DIR="$CACHE" "$BIN" cli index_repository "{\"repo_path\":\"$CORPUS\",\"mode\":\"full\"}" >/dev/null 2>&1 || true

DB="$(ls "$CACHE"/*.db 2>/dev/null | grep -v _config | head -1)"
[ -z "$DB" ] && { echo "no graph db produced in $CACHE"; exit 1; }

q(){ sqlite3 "$DB" "$1"; }
# Match both the bare QN and the name/arity-suffixed QN (D3, Phase 1c): a
# Function `User.new` now has qualified_name `...user.new/2`.
node_exists(){ [ "$(q "SELECT COUNT(*) FROM nodes WHERE label='$1' AND (qualified_name LIKE '%$2' OR qualified_name LIKE '%$2/%')")" -gt 0 ] && echo yes || echo NO; }

echo "# elixir_showcase — $BIN"
echo "nodes=$(q 'SELECT COUNT(*) FROM nodes')  edges=$(q 'SELECT COUNT(*) FROM edges')"
echo "Functions=$(q "SELECT COUNT(*) FROM nodes WHERE label IN ('Function','Method')")  Classes=$(q "SELECT COUNT(*) FROM nodes WHERE label='Class'")"
echo "CALLS=$(q "SELECT COUNT(*) FROM edges WHERE type='CALLS'")  DEFINES=$(q "SELECT COUNT(*) FROM edges WHERE type='DEFINES'")"
# IMPORTS accuracy (Phase 2.5d): in-repo directives resolve to the declaring
# module's Class node; stdlib/undefined targets form no edge. List targets so
# a mis-pointed edge is visible, not just counted.
echo "IMPORTS=$(q "SELECT COUNT(*) FROM edges WHERE type='IMPORTS'")  targets: $(q "SELECT GROUP_CONCAT(DISTINCT t.name) FROM edges e JOIN nodes t ON e.target_id=t.id WHERE e.type='IMPORTS'")"
# NOTE: IMPORTS *edges* need a resolved in-repo target node, which is Phase-2
# cross-module resolution — most of this corpus's directives target stdlib
# (GenServer/Logger) or intentionally-undefined modules, so edge count is not a
# Phase-0 signal here. Import *extraction* is covered by test_grammar_imports.c
# and the external corpus (plug IMPORTS 40->92). The node checks below are the
# deterministic Phase-0 oracle.
echo "-- capability checks (yes = works) --"
echo "guarded def User.new              : $(node_exists Function 'User.new')"
echo "defguard User.is_adult           : $(node_exists Function 'User.is_adult')"
echo "guarded callback handle_call     : $(node_exists Function 'handle_call')"
echo "defmacro Math.const              : $(node_exists Function 'Math.const')"
echo "nested module Server.State       : $(node_exists Class 'Showcase.Server.State')"
echo "protocol Showcase.Describable    : $(node_exists Class 'Showcase.Describable')"
echo "defimpl Describable.User         : $(node_exists Class 'Describable.Showcase.Accounts.User')"
echo "defimpl Describable.BitString    : $(node_exists Class 'Describable.BitString')"
