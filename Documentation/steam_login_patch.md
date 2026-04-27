# Patch `steam_login` para XASH3D-ENCHANCED

Este pacote adiciona uma base **oficial** para alternar entre conexão padrão e conexão Steam:

```txt
steam_login 0   // padrão: comportamento atual / conexão padrão
steam_login 1   // tenta usar Steamworks + Steam Auth Session Ticket oficial
```

## O que este patch faz

1. Cria a CVar `steam_login` com padrão `0`.
2. Adiciona uma shim C++ (`steam_login_client.cpp`) que chama `SteamAPI_Init()` e `GetAuthSessionTicket()` quando o engine é compilado com `XASH_ENABLE_STEAM_AUTH`.
3. Injeta, quando possível, uma troca dentro de `CL_SendGoldSrcConnectPacket(...)`: se `steam_login` estiver em `1`, o pacote de conexão usa um ticket oficial da Steam em vez do ticket non-Steam já existente.
4. Mantém fallback limpo: sem `XASH_ENABLE_STEAM_AUTH`, `steam_login 0` continua compilando/rodando normalmente.

## O que este patch NÃO faz

- Não inclui Steamworks SDK.
- Não inclui `steam_api.dll`, `libsteam_api.so` ou `steam_appid.txt`.
- Não falsifica SteamID, não cria ticket fake e não burla VAC/Fastcup.
- Não garante sozinho compatibilidade com todo servidor Steam/GoldSrc existente se o `wire format` do fork divergir do GoldSrc Steam real.

## Como aplicar

```bash
python3 patch_steam_login.py --xash ~/c/XASH3D-ENCHANCED --cs16 ~/c/cs16-client-Enchanced
```

Depois confira:

```bash
cd ~/c/XASH3D-ENCHANCED
git diff
./waf build
```

## Para ativar Steamworks de verdade

Você precisa ter acesso legítimo ao Steamworks SDK. Em Linux 32-bit, use a `libsteam_api.so` 32-bit; em engine 64-bit, use a 64-bit.

Exemplo conceitual:

```bash
export STEAMWORKS_SDK="$HOME/SteamworksSDK"
export CXXFLAGS="$CXXFLAGS -DXASH_ENABLE_STEAM_AUTH -I$STEAMWORKS_SDK/public"
export LDFLAGS="$LDFLAGS -L$STEAMWORKS_SDK/redistributable_bin/linux32 -lsteam_api"
./waf configure
./waf build
cp "$STEAMWORKS_SDK/redistributable_bin/linux32/libsteam_api.so" ./build/  # ajuste para seu destdir real
```

Para SDKs antigos, talvez seja preciso adicionar:

```bash
-DXASH_STEAMWORKS_OLD_TICKET_API
```

## Uso no jogo

```txt
steam_login 0
connect IP:PORT
```

ou:

```txt
steam_login 1
connect IP:PORT
```

Se `steam_login 1` falhar, confira:

- Steam aberto e usuário logado.
- `steam_appid.txt` correto durante desenvolvimento.
- `libsteam_api.so` ao lado do binário ou no `LD_LIBRARY_PATH`.
- Arquitetura igual: Xash 32-bit precisa de Steamworks 32-bit.

## Observação importante sobre Fastcup

Para entrar em servidor Steam protegido, você precisa de autenticação oficial válida. Este patch só prepara o caminho correto. Se o servidor exigir módulos/anti-cheat próprios, handshake extra, versão exata do cliente ou validações adicionais, isso fica fora do Steamworks básico.
