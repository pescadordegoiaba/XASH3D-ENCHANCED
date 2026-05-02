# XASH3D i7-3770 Engine Patch v2

Aplicado por `patch.py`.

## O que mudou diretamente na engine

- adiciona `engine/common/xash_i73770_opt.c`;
- adiciona `engine/common/xash_i73770_opt.h`;
- remove os arquivos antigos `engine/xash_i73770_opt.c/.h` se forem do patch antigo;
- injeta pragmas em arquivos C críticos reais da engine;
- atualiza `build_xash_32_curl_archive.sh`.

## Por que mudou para engine/common?

O `engine/wscript` compila `common/*.c`, `common/imagelib/*.c`, `common/soundlib/*.c` e `server/*.c`. Portanto, colocar o runtime em `engine/common/` faz ele entrar no build automaticamente.

## Teste se entrou no build

Depois do build, o log deve ter:

```text
Processing engine/common/xash_i73770_opt.c
```

Ou confira com:

```bash
grep -R "xash_i73770_opt.c" build/compile_commands.json
```

## Build normal

```bash
./build_xash_32_curl_archive.sh
```

## Teste runtime

```bash
XASH_I73770_VERBOSE=1 ./build/game_launch/xash3d -dev 3 -console
```

A mensagem esperada contém:

```text
[xash-i73770] engine patch ativo
```

## PGO

```bash
XASH_PGO=gen ./build_xash_32_curl_archive.sh
# rode mapas reais por alguns minutos
XASH_PGO=use ./build_xash_32_curl_archive.sh
```

## Fast math

```bash
XASH_FAST_MATH=1 ./build_xash_32_curl_archive.sh
```

Use com cuidado: pode alterar física, demos e pequenos cálculos de rede.

## Reverter

```bash
python3 patch.py --revert-last
```
