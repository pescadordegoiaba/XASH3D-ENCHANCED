#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

APPID="${APPID:-10}"
LISTEN="${LISTEN:-127.0.0.1:27420}"

if [[ ! -x ./steam_broker ]]; then
  echo "ERRO: ./steam_broker não existe."
  echo "Compile primeiro:"
  echo '  make STEAMWORKS_SDK="$HOME/SteamworksSDK/sdk" ARCH=32'
  exit 1
fi

export LD_LIBRARY_PATH="$PWD:${LD_LIBRARY_PATH:-}"

echo "[run] APPID=$APPID LISTEN=$LISTEN"
echo "[run] Abra o Steam antes de continuar."
exec ./steam_broker --listen "$LISTEN" --appid "$APPID"
