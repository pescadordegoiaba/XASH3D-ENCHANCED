#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

STATE="$ROOT/.xash_i73770_patch"
if [[ ! -d "$STATE/backups" ]]; then
  echo "Nenhum backup encontrado em $STATE/backups"
  exit 1
fi

LAST="$(find "$STATE/backups" -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
if [[ -z "$LAST" ]]; then
  echo "Nenhum backup encontrado."
  exit 1
fi

echo "[xash-i73770] Restaurando backup: $LAST"
rsync -a "$LAST"/ "$ROOT"/

echo "[xash-i73770] Removendo arquivos novos do patch..."
rm -rf "$ROOT/tools/i73770"
rm -f "$ROOT/scripts/xash_i73770_build.sh"
rm -f "$ROOT/scripts/xash_i73770_run.sh"
rm -f "$ROOT/scripts/xash_i73770_bench.sh"
rm -f "$ROOT/scripts/xash_i73770_pgo.sh"
rm -f "$ROOT/scripts/xash_i73770_revert.sh"
rm -f "$ROOT/Documentation/I73770_OPTIMIZATION.md"

echo "[xash-i73770] Revert concluído."
