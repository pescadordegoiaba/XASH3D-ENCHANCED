# SteamBroker legacy GoldSrc

Este broker usa `ISteamUser::InitiateGameConnection` / `TerminateGameConnection`, a API legacy da Steamworks para autenticação de cliente em servidor de jogo.

Compile:

```bash
cd /home/gullin/claude/XASH3D-ENCHANCED/external/steam_broker
make clean
make STEAMWORKS_SDK=/home/gullin/Downloads/sdk ARCH=32
```

Rode:

```bash
APPID=10 LISTEN=127.0.0.1:27420 ./run_steam_broker.sh
```

No Xash:

```text
cl_steam_broker_addr 127.0.0.1:27420
steam_login 1
connect IP:PORT
```

Se o servidor ainda rejeitar o cliente, o bloqueio pode ser por cliente não oficial, anti-cheat próprio, módulo obrigatório, versão exata do cliente ou checagem extra do servidor.
