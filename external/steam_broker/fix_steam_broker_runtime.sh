#!/usr/bin/env bash
# fix_steam_broker_runtime.sh
# Execute dentro de:
#   /home/gullin/claude/XASH3D-ENCHANCED/external/steam_broker
#
# Uso:
#   cd /home/gullin/claude/XASH3D-ENCHANCED/external/steam_broker
#   bash /mnt/data/fix_steam_broker_runtime.sh
#
# Variáveis opcionais:
#   SDK=/home/gullin/Downloads/sdk ARCH=32 APPID=10 LISTEN=127.0.0.1:27420 bash /mnt/data/fix_steam_broker_runtime.sh

set -Eeuo pipefail

SDK="${SDK:-/home/gullin/Downloads/sdk}"
ARCH="${ARCH:-32}"
APPID="${APPID:-10}"
LISTEN="${LISTEN:-127.0.0.1:27420}"

if [[ ! -f "steam_broker.cpp" ]]; then
  echo "[ERRO] Execute este script dentro da pasta external/steam_broker."
  echo "PWD atual: $(pwd)"
  exit 1
fi

if [[ ! -f "$SDK/public/steam/steam_api.h" ]]; then
  echo "[ERRO] Não achei steam_api.h em:"
  echo "  $SDK/public/steam/steam_api.h"
  echo "Defina SDK=/caminho/da/pasta/sdk"
  exit 1
fi

case "$ARCH" in
  32)
    MFLAG="-m32"
    STEAM_LIB_DIR="$SDK/redistributable_bin/linux32"
    ;;
  64)
    MFLAG="-m64"
    STEAM_LIB_DIR="$SDK/redistributable_bin/linux64"
    ;;
  *)
    echo "[ERRO] ARCH deve ser 32 ou 64."
    exit 1
    ;;
esac

if [[ ! -f "$STEAM_LIB_DIR/libsteam_api.so" ]]; then
  echo "[ERRO] Não achei libsteam_api.so em:"
  echo "  $STEAM_LIB_DIR/libsteam_api.so"
  exit 1
fi

echo "[*] PWD=$(pwd)"
echo "[*] SDK=$SDK"
echo "[*] ARCH=$ARCH"
echo "[*] Steam lib dir=$STEAM_LIB_DIR"

# Recria Makefile se ele foi perdido.
if [[ ! -f Makefile ]]; then
  echo "[*] Makefile não existe. Recriando..."
  cat > Makefile <<'MAKEEOF'
CXX ?= g++
STEAMWORKS_SDK ?=
ARCH ?= 32

ifeq ($(strip $(STEAMWORKS_SDK)),)
$(error Defina STEAMWORKS_SDK. Exemplo: make STEAMWORKS_SDK=/home/gullin/Downloads/sdk ARCH=32)
endif

ifeq ($(ARCH),32)
MFLAG := -m32
STEAM_LIB_DIR := $(STEAMWORKS_SDK)/redistributable_bin/linux32
else
MFLAG := -m64
STEAM_LIB_DIR := $(STEAMWORKS_SDK)/redistributable_bin/linux64
endif

CXXFLAGS ?= -O2 -g -std=c++17
CPPFLAGS += $(MFLAG) -I$(STEAMWORKS_SDK)/public -Wall -Wextra
LDFLAGS += $(MFLAG) -L$(STEAM_LIB_DIR) -Wl,-rpath,'$$ORIGIN'
LDLIBS += -lsteam_api -ldl -pthread

all: steam_broker libsteam_api.so

steam_broker: steam_broker.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

libsteam_api.so:
	cp -f "$(STEAM_LIB_DIR)/libsteam_api.so" ./libsteam_api.so

clean:
	rm -f steam_broker libsteam_api.so steam_appid.txt

.PHONY: all clean
MAKEEOF
fi

echo "[*] Copiando libsteam_api.so para a pasta do broker..."
cp -f "$STEAM_LIB_DIR/libsteam_api.so" ./libsteam_api.so

echo "[*] Compilando steam_broker..."
make STEAMWORKS_SDK="$SDK" ARCH="$ARCH"

echo "[*] Criando run_steam_broker.sh corrigido..."
cat > run_steam_broker.sh <<'RUNEOF'
#!/usr/bin/env bash
set -e

APPID="${APPID:-10}"
LISTEN="${LISTEN:-127.0.0.1:27420}"

cd "$(dirname "$0")"

echo "[run] APPID=$APPID LISTEN=$LISTEN"
echo "[run] Abra o Steam antes de continuar."

if [ ! -f ./steam_broker ]; then
  echo "[ERRO] ./steam_broker não existe. Compile primeiro."
  exit 1
fi

if [ ! -f ./libsteam_api.so ]; then
  echo "[ERRO] ./libsteam_api.so não existe nesta pasta."
  echo "Copie do Steamworks SDK:"
  echo "  cp /home/gullin/Downloads/sdk/redistributable_bin/linux32/libsteam_api.so ."
  exit 1
fi

printf "%s\n" "$APPID" > steam_appid.txt

# Garante que o loader ache a libsteam_api.so local.
export LD_LIBRARY_PATH="$PWD:${LD_LIBRARY_PATH:-}"

exec ./steam_broker --listen "$LISTEN"
RUNEOF

chmod +x run_steam_broker.sh

echo "[*] Verificando arquivos:"
ls -lh steam_broker libsteam_api.so run_steam_broker.sh Makefile
file steam_broker libsteam_api.so || true

echo
echo "[OK] Broker corrigido."
echo "Agora rode:"
echo "  cd $(pwd)"
echo "  APPID=$APPID LISTEN=$LISTEN ./run_steam_broker.sh"
