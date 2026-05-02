#!/usr/bin/env bash
set -e

APPID="${APPID:-10}"
LISTEN="${LISTEN:-127.0.0.1:27420}"

cd "$(dirname "$0")"

echo "[run] APPID=$APPID LISTEN=$LISTEN"
echo "[run] Abra o Steam antes de continuar."

if [ ! -x ./steam_broker ]; then
  echo "[ERRO] ./steam_broker não existe ou não é executável."
  echo "Compile com:"
  echo "  make STEAMWORKS_SDK=/home/gullin/Downloads/sdk ARCH=32"
  exit 1
fi

if [ ! -f ./libsteam_api.so ]; then
  echo "[ERRO] ./libsteam_api.so não existe nesta pasta."
  echo "Compile com:"
  echo "  make STEAMWORKS_SDK=/home/gullin/Downloads/sdk ARCH=32"
  exit 1
fi

printf "%s\n" "$APPID" > steam_appid.txt

export LD_LIBRARY_PATH="$PWD:${LD_LIBRARY_PATH:-}"

exec ./steam_broker --listen "$LISTEN"
