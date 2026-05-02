#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v perf >/dev/null 2>&1; then
  echo "perf não encontrado. Instale linux-tools/perf equivalente da sua distro."
  exit 1
fi

echo "[xash-i73770] Benchmark com perf."
echo "Exemplo: ./scripts/xash_i73770_bench.sh -- -game cstrike +map de_dust2"
echo

perf stat -d -d -d \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
  ./scripts/xash_i73770_run.sh "$@"
