#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig

export XASH_DL_CFLAGS="$(pkg-config --cflags libcurl libarchive) -DXASH_USE_CURL_DOWNLOADER=1"
export XASH_DL_LIBS="$(pkg-config --libs libcurl libarchive)"

echo "[*] libcurl libdir:    $(pkg-config --variable=libdir libcurl)"
echo "[*] libarchive libdir: $(pkg-config --variable=libdir libarchive)"
echo "[*] CFLAGS: $XASH_DL_CFLAGS"
echo "[*] LIBS:   $XASH_DL_LIBS"

CFLAGS="${CFLAGS:-} $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_DL_LIBS" \
./waf configure --disable-werror

CFLAGS="${CFLAGS:-} $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_DL_LIBS" \
./waf build -j$(nproc)
