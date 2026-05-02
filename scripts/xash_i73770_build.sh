#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BITS="32"
LTO="1"
PGO="none"
FAST_MATH="0"
CLEAN="0"
EXTRA_CONFIG=()

usage() {
  cat <<EOF
Uso:
  ./scripts/xash_i73770_build.sh [opções] [-- opções extras do waf configure]

Opções:
  --32              build 32-bit, padrão do Xash em x86/x86_64
  --64              build 64-bit; requer game/client DLLs 64-bit compatíveis
  --no-lto          desativa LTO
  --fast-math       ativa -ffast-math; teste movimento/física antes de usar fixo
  --pgo-generate    build instrumentado para gerar perfil
  --pgo-use         usa perfil coletado em build/pgo-i73770
  --clean           roda ./waf clean antes do build
  -h, --help        ajuda

Exemplos:
  ./scripts/xash_i73770_build.sh
  ./scripts/xash_i73770_build.sh --64
  ./scripts/xash_i73770_build.sh --pgo-generate
  ./scripts/xash_i73770_build.sh --pgo-use
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --32) BITS="32"; shift ;;
    --64) BITS="64"; shift ;;
    --no-lto) LTO="0"; shift ;;
    --fast-math) FAST_MATH="1"; shift ;;
    --pgo-generate) PGO="generate"; shift ;;
    --pgo-use) PGO="use"; shift ;;
    --clean) CLEAN="1"; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; EXTRA_CONFIG+=("$@"); break ;;
    *) EXTRA_CONFIG+=("$1"); shift ;;
  esac
done

PGO_DIR="$ROOT/build/pgo-i73770"
mkdir -p "$PGO_DIR" "$ROOT/build/i73770"

COMMON_FLAGS="-O3 -march=ivybridge -mtune=ivybridge -pipe -fno-plt -fomit-frame-pointer -fstrict-aliasing -DNDEBUG -DXASH_I73770_OPT=1"
LINK_FLAGS="-Wl,-O1 -Wl,--as-needed -fno-plt"

if [[ "$FAST_MATH" == "1" ]]; then
  COMMON_FLAGS="$COMMON_FLAGS -ffast-math -fno-math-errno -fno-trapping-math"
fi

if [[ "$LTO" == "1" ]]; then
  COMMON_FLAGS="$COMMON_FLAGS -flto"
  LINK_FLAGS="$LINK_FLAGS -flto"
fi

case "$PGO" in
  generate)
    COMMON_FLAGS="$COMMON_FLAGS -fprofile-generate=$PGO_DIR"
    LINK_FLAGS="$LINK_FLAGS -fprofile-generate=$PGO_DIR"
    ;;
  use)
    COMMON_FLAGS="$COMMON_FLAGS -fprofile-use=$PGO_DIR -fprofile-correction"
    LINK_FLAGS="$LINK_FLAGS -fprofile-use=$PGO_DIR"
    ;;
esac

export CFLAGS="$COMMON_FLAGS ${CFLAGS:-}"
export CXXFLAGS="$COMMON_FLAGS ${CXXFLAGS:-}"
export LINKFLAGS="$LINK_FLAGS ${LINKFLAGS:-}"
export LDFLAGS="$LINK_FLAGS ${LDFLAGS:-}"

build_preload() {
  local bits_flag=()
  local out="$ROOT/build/i73770/libxash_i73770_preload.so"

  if [[ "$BITS" == "32" ]]; then
    bits_flag=(-m32)
  fi

  echo "[xash-i73770] Compilando preload runtime (${BITS}-bit)..."
  if ${CC:-cc} "${bits_flag[@]}" -O3 -march=ivybridge -mtune=ivybridge -fPIC -shared \
    "$ROOT/tools/i73770/xash_i73770_preload.c" \
    -o "$out" -ldl >/tmp/xash_i73770_preload_build.log 2>&1; then
    echo "[xash-i73770] Preload OK: $out"
  else
    echo "[xash-i73770] Aviso: não consegui compilar preload runtime ${BITS}-bit."
    echo "[xash-i73770] O build da engine continua. Detalhes:"
    sed -n '1,120p' /tmp/xash_i73770_preload_build.log || true
    echo
    echo "[xash-i73770] Em Manjaro, para 32-bit talvez precise: sudo pacman -S lib32-glibc lib32-gcc-libs"
  fi
}

CONFIG_ARGS=(--disable-werror)

if [[ "$BITS" == "64" ]]; then
  CONFIG_ARGS+=(-8)
else
  CONFIG_ARGS+=(-4)
fi

CONFIG_ARGS+=("${EXTRA_CONFIG[@]}")

echo "[xash-i73770] Root: $ROOT"
echo "[xash-i73770] Bits: $BITS"
echo "[xash-i73770] LTO: $LTO"
echo "[xash-i73770] PGO: $PGO"
echo "[xash-i73770] FAST_MATH: $FAST_MATH"
echo "[xash-i73770] CFLAGS: $CFLAGS"
echo "[xash-i73770] LINKFLAGS: $LINKFLAGS"
echo

build_preload

if [[ "$CLEAN" == "1" ]]; then
  ./waf clean || true
fi

./waf configure "${CONFIG_ARGS[@]}"
./waf build -j"$(nproc)"
