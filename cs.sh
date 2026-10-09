#!/usr/bin/env bash
# Sobe o CS 1.6. Se o cliente Steam estiver aberto e logado, inicia o
# SteamBroker (Steamworks oficial, AppID 10) e o jogo entra pedindo ticket
# Steam. Sem Steam, ou se a API recusar a conta, o jogo abre no modo normal.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$SCRIPT_DIR"
ENGINE="$GAME_DIR/xash3d"
BROKER_DIR="${STEAM_BROKER_DIR:-$HOME/cs1.6/Build/XASH3D-ENCHANCED/external/steam_broker}"
STEAM_LIB="${STEAM_LIB:-$HOME/.local/share/Steam/steamrt32/libsteam_api.so}"
BROKER_ADDR="${STEAM_BROKER_ADDR:-127.0.0.1:27420}"
BROKER_PORT="${BROKER_ADDR##*:}"
APPID=10
BROKER_PID=""
BROKER_STARTED=0

export XASH3D_BASEDIR="$GAME_DIR"
export LD_LIBRARY_PATH="$GAME_DIR:${LD_LIBRARY_PATH:-}"

cd "$GAME_DIR" || {
	echo "Erro: não consegui entrar na pasta do Xash: $GAME_DIR"
	exit 1
}

if [ ! -x "$ENGINE" ]; then
	echo "Erro: não achei o binário da engine em $ENGINE"
	exit 1
fi

if [ ! -d "$GAME_DIR/cstrike" ]; then
	echo "Erro: não achei a pasta cstrike em $GAME_DIR/cstrike"
	exit 1
fi

steam_client_up() {
	pgrep -f '/steam\.sh$' >/dev/null 2>&1 || pgrep -x steam >/dev/null 2>&1
}

broker_port_open() {
	ss -lun 2>/dev/null | grep -q ":${BROKER_PORT} "
}

stop_broker() {
	if [ "$BROKER_STARTED" = 1 ] && [ -n "$BROKER_PID" ]; then
		kill "$BROKER_PID" >/dev/null 2>&1 || true
		wait "$BROKER_PID" >/dev/null 2>&1 || true
	fi
}

start_steam_session() {
	local log="$BROKER_DIR/steam_broker.log"

	if ! steam_client_up; then
		echo "[steam] Cliente Steam não está aberto. O jogo entra sem validar sessão Steam."
		return 1
	fi

	if [ ! -x "$BROKER_DIR/steam_broker" ]; then
		echo "[steam] Steam está aberto, mas não achei o broker em:"
		echo "        $BROKER_DIR/steam_broker"
		return 1
	fi

	if [ ! -f "$STEAM_LIB" ]; then
		echo "[steam] Não achei a libsteam_api.so de 32 bits em:"
		echo "        $STEAM_LIB"
		return 1
	fi

	printf '%s\n' "$APPID" > "$BROKER_DIR/steam_appid.txt"
	printf '%s\n' "$APPID" > "$GAME_DIR/steam_appid.txt"
	printf '%s\n' "$APPID" > "$GAME_DIR/cstrike/steam_appid.txt"

	if broker_port_open; then
		echo "[steam] Broker já está ouvindo em $BROKER_ADDR."
		return 0
	fi

	echo "[steam] Steam aberto. Iniciando Steamworks (AppID $APPID, Counter-Strike)..."
	(
		cd "$BROKER_DIR" || exit 1
		export SteamAppId="$APPID"
		export SteamGameId="$APPID"
		export LD_LIBRARY_PATH="$BROKER_DIR:$(dirname "$STEAM_LIB"):${LD_LIBRARY_PATH:-}"
		exec ./steam_broker --listen "$BROKER_ADDR"
	) >>"$log" 2>&1 &
	BROKER_PID=$!
	BROKER_STARTED=1

	local i
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
		if ! kill -0 "$BROKER_PID" >/dev/null 2>&1; then
			echo "[steam] O Steamworks recusou a sessão. Veja $log"
			echo "[steam] É preciso Steam logada e CS 1.6 (AppID 10) nesta conta."
			BROKER_STARTED=0
			return 1
		fi
		if broker_port_open; then
			echo "[steam] Sessão Steam aceita. O jogo vai usar ticket oficial ao conectar."
			return 0
		fi
		sleep 0.4
	done

	echo "[steam] O broker não confirmou o login a tempo. Veja $log"
	stop_broker
	BROKER_STARTED=0
	return 1
}

STEAM_ARGS=()
if start_steam_session; then
	STEAM_ARGS=(
		+set steam_login 1
		+set cl_ticket_generator steam
		+set cl_steam_broker_addr "$BROKER_ADDR"
	)
else
	echo "[steam] Continuando sem steam_login."
fi

"$ENGINE" -game cstrike -console "$@" "${STEAM_ARGS[@]}"
status=$?
stop_broker
exit "$status"
