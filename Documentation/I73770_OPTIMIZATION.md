# Xash3D i7-3770 Optimization Patch

Este patch foi feito para o fluxo:

```bash
cd /home/gullin/claude/XASH3D-ENCHANCED/
python3 patch.py
./scripts/xash_i73770_build.sh
./scripts/xash_i73770_run.sh -- -game cstrike
```

## O que foi adicionado

- `scripts/xash_i73770_build.sh`
  - `-O3`
  - `-march=ivybridge`
  - `-mtune=ivybridge`
  - LTO opcional, ativado por padrão
  - PGO por `--pgo-generate` e `--pgo-use`
  - `--fast-math` opcional

- `scripts/xash_i73770_run.sh`
  - `taskset -c 0-7`
  - `gamemoderun` se instalado
  - `mesa_glthread=true`
  - `vblank_mode=0`
  - `MALLOC_ARENA_MAX=2`
  - `LD_PRELOAD` do runtime se compilado

- `tools/i73770/xash_i73770_preload.c`
  - ativa FTZ/DAZ para reduzir custo de denormal floats
  - aplica `mallopt` no glibc
  - não muda lógica do jogo

- `tools/i73770/xash_i73770_jobs.hpp`
  - base de thread pool para futuras integrações manuais

- `tools/i73770/xash_i73770_simd.h`
  - helpers SIMD para futuras integrações manuais

## Build normal

```bash
./scripts/xash_i73770_build.sh
```

## Build 64-bit

Só use se o client/game também forem 64-bit:

```bash
./scripts/xash_i73770_build.sh --64
```

## PGO automático

```bash
./scripts/xash_i73770_pgo.sh -game cstrike +map de_dust2
```

Ou manual:

```bash
./scripts/xash_i73770_build.sh --pgo-generate
./scripts/xash_i73770_run.sh -- -game cstrike +map de_dust2
./scripts/xash_i73770_build.sh --pgo-use
```

## Benchmark

```bash
./scripts/xash_i73770_bench.sh -- -game cstrike +map de_dust2
```

## Reverter

```bash
./scripts/xash_i73770_revert.sh
```

## Observação

Este patch cria a base de otimização segura. Paralelizar renderer/física/rede de verdade exige alterações manuais em funções específicas, guiadas por `perf`, porque mexer em ordem de execução pode quebrar determinismo, mods e comportamento de rede.
