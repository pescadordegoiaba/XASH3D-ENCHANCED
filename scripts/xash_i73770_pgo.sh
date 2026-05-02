#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MAP_ARGS=("$@")
if [[ ${#MAP_ARGS[@]} -eq 0 ]]; then
  MAP_ARGS=(-game cstrike +map de_dust2)
fi

echo "[xash-i73770] Etapa 1/3: build PGO generate"
./scripts/xash_i73770_build.sh --pgo-generate

echo
echo "[xash-i73770] Etapa 2/3: rode a engine e jogue um pouco."
echo "[xash-i73770] Feche o jogo quando terminar de coletar perfil."
./scripts/xash_i73770_run.sh -- "${MAP_ARGS[@]}" || true

echo
echo "[xash-i73770] Etapa 3/3: build PGO use"
./scripts/xash_i73770_build.sh --pgo-use
