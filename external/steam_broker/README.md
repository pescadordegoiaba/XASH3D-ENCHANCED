# SteamBroker externo para XASH3D-ENCHANCED

Este diretório foi criado pelo patch automático.

## O que é

O `steam_broker` é um programa externo que roda separado do Xash.

Fluxo:

```text
Steam aberto/logado
        ↓
external/steam_broker/steam_broker ouvindo 127.0.0.1:27420
        ↓
Xash: steam_login 1
        ↓
Xash pede ticket ao broker
        ↓
broker chama SteamAPI_Init + GetAuthSessionTicket
        ↓
broker devolve SteamID64 + ticket ao Xash
        ↓
Xash manda o connect GoldSrc com o ticket
```

Isso não falsifica SteamID e não burla VAC/anti-cheat. Ele só usa a API oficial do Steamworks.

## Compilar

Baixe o Steamworks SDK oficial pela sua conta Steamworks e aponte a variável `STEAMWORKS_SDK` para a pasta `sdk`.

Exemplo comum:

```bash
cd /home/gullin/claude/XASH3D-ENCHANCED/external/steam_broker

make STEAMWORKS_SDK="$HOME/SteamworksSDK/sdk" ARCH=32
```

Use `ARCH=32` se seu Xash/gamecode for 32-bit, que é o caminho mais comum para Half-Life/CS 1.6.

Se o seu sistema não tiver toolchain 32-bit no Manjaro:

```bash
sudo pacman -S --needed gcc-multilib lib32-glibc lib32-gcc-libs
```

## Rodar

Terminal 1:

```bash
steam
```

Terminal 2:

```bash
cd /home/gullin/claude/XASH3D-ENCHANCED/external/steam_broker
./run_steam_broker.sh
```

Por padrão o script usa:

```bash
APPID=10
LISTEN=127.0.0.1:27420
```

Você pode trocar:

```bash
APPID=10 LISTEN=127.0.0.1:27420 ./run_steam_broker.sh
```

## Xash

Recompile o Xash depois do patch:

```bash
cd /home/gullin/claude/XASH3D-ENCHANCED
./waf clean
./waf configure
./waf build
```

No console do Xash:

```text
cl_steam_broker_addr 127.0.0.1:27420
steam_login 1
connect 91.211.247.221:27015
```

Para voltar ao modo padrão:

```text
steam_login 0
```

## Diagnóstico

Ver se o broker está ouvindo:

```bash
ss -lunp | grep 27420
```

Ver se o Xash foi compilado com a cvar:

```bash
find build -type f -executable -print0 | xargs -0 strings 2>/dev/null | grep -F "steam_login" | head
```

Se `SteamAPI_Init` falhar:

1. abra o Steam e faça login;
2. confira se `libsteam_api.so` está no diretório do broker;
3. confira a arquitetura: broker 32-bit precisa de `linux32/libsteam_api.so`;
4. confira se `steam_appid.txt` foi criado pelo `--appid 10`.

## Limitação importante

Esse broker gera ticket oficial, mas não garante que servidores como Fastcup aceitem Xash. Servidores protegidos podem exigir cliente oficial, módulos/anti-cheat próprios, versão exata de protocolo ou validações extras.
