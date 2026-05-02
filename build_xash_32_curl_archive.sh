#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig

export XASH_DL_CFLAGS="$(pkg-config --cflags libcurl libarchive) -DXASH_USE_CURL_DOWNLOADER=1"
export XASH_DL_LIBS="$(pkg-config --libs libcurl libarchive)"

XASH_LTO="${XASH_LTO:-1}"
XASH_FAST_MATH="${XASH_FAST_MATH:-0}"
XASH_PGO="${XASH_PGO:-off}"
XASH_PGO_DIR="${XASH_PGO_DIR:-$(pwd)/build/pgo-i73770}"

XASH_OPT_CFLAGS="-O3 -march=ivybridge -mtune=ivybridge -mfpmath=sse -pipe -pthread -fno-plt -fomit-frame-pointer -fstrict-aliasing -fno-semantic-interposition -fno-math-errno -fno-trapping-math -DNDEBUG -DXASH_I73770_ENGINE_PATCH=1 -DXASH_PARTICLE_FPS_PATCH=1"
XASH_OPT_LINKFLAGS="-pthread -Wl,-O1 -Wl,--as-needed -fno-plt"

if [ "$XASH_FAST_MATH" = "1" ]; then
  XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -ffast-math"
fi

if [ "$XASH_LTO" = "1" ]; then
  XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -flto=auto"
  XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -flto=auto"
fi

mkdir -p "$XASH_PGO_DIR"

case "$XASH_PGO" in
  gen|generate)
    XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -fprofile-generate=$XASH_PGO_DIR"
    XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -fprofile-generate=$XASH_PGO_DIR"
    ;;
  use)
    XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -fprofile-use=$XASH_PGO_DIR -fprofile-correction"
    XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -fprofile-use=$XASH_PGO_DIR"
    ;;
  off|"")
    ;;
  *)
    echo "[!] XASH_PGO inválido: $XASH_PGO"
    echo "    Use: XASH_PGO=off | gen | use"
    exit 1
    ;;
esac

echo "[*] libcurl libdir:    $(pkg-config --variable=libdir libcurl)"
echo "[*] libarchive libdir: $(pkg-config --variable=libdir libarchive)"
echo "[*] DL CFLAGS: $XASH_DL_CFLAGS"
echo "[*] DL LIBS:   $XASH_DL_LIBS"
echo "[*] OPT CFLAGS: $XASH_OPT_CFLAGS"
echo "[*] OPT LDFLAGS: $XASH_OPT_LINKFLAGS $XASH_DL_LIBS"
echo "[*] LTO=$XASH_LTO FAST_MATH=$XASH_FAST_MATH PGO=$XASH_PGO"
echo

CFLAGS="${CFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
CXXFLAGS="${CXXFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
LDFLAGS="${LDFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
./waf configure --disable-werror

CFLAGS="${CFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
CXXFLAGS="${CXXFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
LDFLAGS="${LDFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
./waf build -j$(nproc)
