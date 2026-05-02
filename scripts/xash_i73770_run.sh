#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XASH_I73770_PRELOAD="${XASH_I73770_PRELOAD:-1}"
export XASH_I73770_VERBOSE="${XASH_I73770_VERBOSE:-1}"
export XASH_I73770_MALLOC_TUNE="${XASH_I73770_MALLOC_TUNE:-1}"

# Mesa/RadeonSI/RX 580: deixa o driver usar thread auxiliar para OpenGL.
export mesa_glthread="${mesa_glthread:-true}"

# Desativa vsync para benchmark/FPS bruto. Para jogar normal, pode usar vblank_mode=1.
export vblank_mode="${vblank_mode:-0}"

# Reduz excesso de arenas do glibc em apps com threads.
export MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-2}"

ARGS=("$@")
if [[ "${ARGS[0]:-}" == "--" ]]; then
  ARGS=("${ARGS[@]:1}")
fi

find_engine() {
  local candidates=(
    "$ROOT/build/xash3d"
    "$ROOT/build/engine/xash3d"
    "$ROOT/build/game_launch/xash3d"
    "$ROOT/xash3d"
    "$ROOT/xash3d-linux"
  )

  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done

  find "$ROOT/build" -maxdepth 5 -type f -executable -name 'xash3d*' 2>/dev/null | head -n 1
}

ENGINE="$(find_engine || true)"
if [[ -z "${ENGINE:-}" ]]; then
  echo "[xash-i73770] Não achei binário xash3d."
  echo "Compile primeiro:"
  echo "  ./scripts/xash_i73770_build.sh"
  exit 1
fi

PRELOAD="$ROOT/build/i73770/libxash_i73770_preload.so"
ENV_PREFIX=()

if [[ "$XASH_I73770_PRELOAD" == "1" && -f "$PRELOAD" ]]; then
  ENV_PREFIX+=(env "LD_PRELOAD=$PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}")
fi

LAUNCH=()
if command -v gamemoderun >/dev/null 2>&1; then
  LAUNCH+=(gamemoderun)
fi

echo "[xash-i73770] Engine: $ENGINE"
echo "[xash-i73770] mesa_glthread=$mesa_glthread vblank_mode=$vblank_mode MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX"
[[ -f "$PRELOAD" ]] && echo "[xash-i73770] Preload: $PRELOAD"

if command -v taskset >/dev/null 2>&1; then
  exec taskset -c 0-7 "${ENV_PREFIX[@]}" "${LAUNCH[@]}" "$ENGINE" "${ARGS[@]}"
else
  exec "${ENV_PREFIX[@]}" "${LAUNCH[@]}" "$ENGINE" "${ARGS[@]}"
fi
