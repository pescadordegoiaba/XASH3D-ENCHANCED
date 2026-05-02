#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# XASH3D-ENCHANCED particle FPS patcher.
#
# Uso:
#   cd /home/gullin/claude/XASH3D-ENCHANCED/
#   python3 patch_particles.py
#
# Reverter:
#   python3 patch_particles.py --revert-last

from __future__ import annotations

import argparse
import datetime as _dt
import re
import shutil
import stat
import sys
from pathlib import Path

PATCH_ID = "XASH_PARTICLE_FPS_PATCH"


def ts() -> str:
    return _dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def die(msg: str) -> None:
    print("[xash-particles] ERRO:", msg, file=sys.stderr)
    raise SystemExit(1)


def is_repo_root(root: Path) -> bool:
    return (root / "waf").exists() and (root / "wscript").exists() and (root / "engine").is_dir()


class PatchCtx:
    def __init__(self, root: Path, dry_run: bool = False):
        self.root = root.resolve()
        self.dry_run = dry_run
        self.state = self.root / ".xash_particle_patch"
        self.backups = self.state / "backups"
        self.backup_dir = self.backups / ts()
        self.changed: list[str] = []
        self.skipped: list[str] = []
        self.notes: list[str] = []
        self.failures: list[str] = []

    def rel(self, path: Path) -> str:
        try:
            return str(path.resolve().relative_to(self.root))
        except Exception:
            return str(path)

    def read(self, path: Path) -> str:
        return path.read_text(encoding="utf-8", errors="replace")

    def backup(self, path: Path) -> None:
        if self.dry_run or not path.exists():
            return
        rel = path.resolve().relative_to(self.root)
        dst = self.backup_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists():
            shutil.copy2(path, dst)

    def write(self, path: Path, data: str, executable: bool = False) -> None:
        old = None
        if path.exists():
            old = self.read(path)
        if old == data:
            self.skipped.append(self.rel(path))
            return
        if self.dry_run:
            self.notes.append(f"[dry-run] escreveria {self.rel(path)}")
            return
        if path.exists():
            self.backup(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8", newline="\n")
        if executable:
            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        self.changed.append(self.rel(path))

    def patch_text(self, path: Path, new_text: str) -> None:
        if not path.exists():
            self.failures.append(f"{self.rel(path)} não existe")
            return
        old = self.read(path)
        if old == new_text:
            self.skipped.append(self.rel(path))
            return
        if self.dry_run:
            self.notes.append(f"[dry-run] modificaria {self.rel(path)}")
            return
        self.backup(path)
        path.write_text(new_text, encoding="utf-8", newline="\n")
        self.changed.append(self.rel(path))


def budget_h() -> str:
    return r'''#ifndef XASH_PARTICLE_BUDGET_H
#define XASH_PARTICLE_BUDGET_H

#ifdef __cplusplus
extern "C" {
#endif

int   XASH_ParticleBudgetEnabled(void);
int   XASH_ParticleClampCount(const char *tag, int count);
int   XASH_ParticleClampDensity(const char *tag, int density);
float XASH_ParticleClampScale(const char *tag, float scale);
void  XASH_ParticleBudgetFrameHint(double realtime);

#ifdef __cplusplus
}
#endif

#endif /* XASH_PARTICLE_BUDGET_H */
'''


def budget_c() -> str:
    return r'''/*
 * XASH particle FPS budget.
 *
 * Controla picos de partículas causados por explosões/efeitos aglomerados.
 * Não depende de structs internas do renderer, então é seguro de linkar em engine/common.
 *
 * Variáveis de ambiente:
 *   XASH_PARTICLE_BUDGET=1        ativa/desativa; default 1
 *   XASH_PARTICLE_SCALE=0.75      multiplica count/density; default 0.75
 *   XASH_PARTICLE_BURST_MAX=384   cap por chamada; default 384
 *   XASH_PARTICLE_WINDOW_MAX=900  cap aproximado por janela curta; default 900
 *   XASH_PARTICLE_VERBOSE=1       imprime clamps
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xash_particle_budget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
#define XASH_PARTICLE_LIKELY(x)   __builtin_expect(!!(x), 1)
#define XASH_PARTICLE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define XASH_PARTICLE_LIKELY(x)   (x)
#define XASH_PARTICLE_UNLIKELY(x) (x)
#endif

static int g_initialized = 0;
static int g_enabled = 1;
static int g_verbose = 0;
static int g_burst_max = 384;
static int g_window_max = 900;
static float g_scale = 0.75f;

static double g_window_start = 0.0;
static int g_window_particles = 0;

static double XASH_ParticleNow(void)
{
#if defined(CLOCK_MONOTONIC)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#else
	return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static int XASH_EnvBool(const char *name, int defval)
{
	const char *v = getenv(name);
	if (!v || !*v) return defval;
	if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "FALSE") || !strcmp(v, "no") || !strcmp(v, "NO"))
		return 0;
	return 1;
}

static int XASH_EnvInt(const char *name, int defval, int minval, int maxval)
{
	const char *v = getenv(name);
	long x;
	if (!v || !*v) return defval;
	x = strtol(v, NULL, 10);
	if (x < minval) x = minval;
	if (x > maxval) x = maxval;
	return (int)x;
}

static float XASH_EnvFloat(const char *name, float defval, float minval, float maxval)
{
	const char *v = getenv(name);
	float x;
	if (!v || !*v) return defval;
	x = (float)strtod(v, NULL);
	if (x < minval) x = minval;
	if (x > maxval) x = maxval;
	return x;
}

static void XASH_ParticleBudgetInit(void)
{
	if (g_initialized)
		return;

	g_initialized = 1;
	g_enabled = XASH_EnvBool("XASH_PARTICLE_BUDGET", 1);
	g_verbose = XASH_EnvBool("XASH_PARTICLE_VERBOSE", 0);
	g_scale = XASH_EnvFloat("XASH_PARTICLE_SCALE", 0.75f, 0.10f, 1.50f);
	g_burst_max = XASH_EnvInt("XASH_PARTICLE_BURST_MAX", 384, 16, 8192);
	g_window_max = XASH_EnvInt("XASH_PARTICLE_WINDOW_MAX", 900, 32, 32768);
	g_window_start = XASH_ParticleNow();

	if (g_verbose)
	{
		fprintf(stderr,
			"[xash-particles] budget=%d scale=%.2f burst_max=%d window_max=%d\n",
			g_enabled, g_scale, g_burst_max, g_window_max);
	}
}

int XASH_ParticleBudgetEnabled(void)
{
	XASH_ParticleBudgetInit();
	return g_enabled;
}

void XASH_ParticleBudgetFrameHint(double realtime)
{
	XASH_ParticleBudgetInit();
	if (realtime <= 0.0)
		realtime = XASH_ParticleNow();

	if (realtime - g_window_start > 0.050)
	{
		g_window_start = realtime;
		g_window_particles = 0;
	}
}

int XASH_ParticleClampCount(const char *tag, int count)
{
	int original = count;
	double now;

	XASH_ParticleBudgetInit();

	if (XASH_PARTICLE_UNLIKELY(!g_enabled))
		return count;

	if (count <= 0)
		return count;

	count = (int)((float)count * g_scale + 0.5f);

	if (count < 1)
		count = 1;

	if (count > g_burst_max)
		count = g_burst_max;

	now = XASH_ParticleNow();
	if (now - g_window_start > 0.050)
	{
		g_window_start = now;
		g_window_particles = 0;
	}

	if (g_window_particles + count > g_window_max)
	{
		int left = g_window_max - g_window_particles;
		if (left < 0) left = 0;
		count = left;
	}

	g_window_particles += count;

	if (g_verbose && original != count)
	{
		fprintf(stderr, "[xash-particles] clamp %s: %d -> %d\n",
			tag ? tag : "?", original, count);
	}

	return count;
}

int XASH_ParticleClampDensity(const char *tag, int density)
{
	return XASH_ParticleClampCount(tag, density);
}

float XASH_ParticleClampScale(const char *tag, float scale)
{
	(void)tag;
	XASH_ParticleBudgetInit();

	if (!g_enabled)
		return scale;

	if (g_scale < 0.95f)
		scale *= 1.0f + ((1.0f - g_scale) * 0.18f);

	return scale;
}
'''


def job_h() -> str:
    return r'''#ifndef XASH_PARTICLE_JOB_H
#define XASH_PARTICLE_JOB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*xash_particle_range_fn)(int begin, int end, void *userdata);

int XASH_ParticleJobEnabled(void);
int XASH_ParticleJobWorkerCount(void);
void XASH_ParticleJobForRange(int begin, int end, int grain, xash_particle_range_fn fn, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* XASH_PARTICLE_JOB_H */
'''


def job_c() -> str:
    return r'''/*
 * Pequeno job helper POSIX para futuras integrações de partículas.
 * Por padrão fica desligado; ativa com XASH_PARTICLE_JOB=1.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xash_particle_job.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct xash_particle_job_s
{
	int begin;
	int end;
	xash_particle_range_fn fn;
	void *userdata;
} xash_particle_job_t;

static int g_init = 0;
static int g_enabled = 0;
static int g_workers = 0;

static int env_bool(const char *name, int defval)
{
	const char *v = getenv(name);
	if (!v || !*v) return defval;
	if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "FALSE") || !strcmp(v, "no") || !strcmp(v, "NO"))
		return 0;
	return 1;
}

static int env_int(const char *name, int defval, int minval, int maxval)
{
	const char *v = getenv(name);
	long x;
	if (!v || !*v) return defval;
	x = strtol(v, NULL, 10);
	if (x < minval) x = minval;
	if (x > maxval) x = maxval;
	return (int)x;
}

static void init_jobs(void)
{
	long n;

	if (g_init)
		return;

	g_init = 1;
	g_enabled = env_bool("XASH_PARTICLE_JOB", 0);

	n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1) n = 1;
	if (n > 8) n = 8;

	g_workers = env_int("XASH_PARTICLE_WORKERS", (n >= 8) ? 3 : 1, 1, 7);
}

int XASH_ParticleJobEnabled(void)
{
	init_jobs();
	return g_enabled;
}

int XASH_ParticleJobWorkerCount(void)
{
	init_jobs();
	return g_workers;
}

static void *job_thread(void *arg)
{
	xash_particle_job_t *job = (xash_particle_job_t *)arg;
	job->fn(job->begin, job->end, job->userdata);
	return NULL;
}

void XASH_ParticleJobForRange(int begin, int end, int grain, xash_particle_range_fn fn, void *userdata)
{
	pthread_t threads[8];
	xash_particle_job_t jobs[8];
	int total, workers, chunks, chunk, i, made = 0;

	init_jobs();

	if (!fn || end <= begin)
		return;

	total = end - begin;
	if (!g_enabled || total < grain || g_workers <= 1)
	{
		fn(begin, end, userdata);
		return;
	}

	workers = g_workers;
	if (workers > 7) workers = 7;

	chunks = workers + 1;
	if (chunks > total) chunks = total;

	chunk = (total + chunks - 1) / chunks;

	for (i = 0; i < chunks - 1; ++i)
	{
		jobs[i].begin = begin + i * chunk;
		jobs[i].end = jobs[i].begin + chunk;
		if (jobs[i].end > end) jobs[i].end = end;
		jobs[i].fn = fn;
		jobs[i].userdata = userdata;

		if (pthread_create(&threads[i], NULL, job_thread, &jobs[i]) == 0)
			made++;
		else
			fn(jobs[i].begin, jobs[i].end, userdata);
	}

	{
		int b = begin + (chunks - 1) * chunk;
		if (b < end)
			fn(b, end, userdata);
	}

	for (i = 0; i < made; ++i)
		pthread_join(threads[i], NULL);
}
'''


def build_script() -> str:
    return r'''#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig

export XASH_DL_CFLAGS="$(pkg-config --cflags libcurl libarchive) -DXASH_USE_CURL_DOWNLOADER=1"
export XASH_DL_LIBS="$(pkg-config --libs libcurl libarchive)"

XASH_LTO="${XASH_LTO:-1}"
XASH_FAST_MATH="${XASH_FAST_MATH:-0}"
XASH_PGO="${XASH_PGO:-off}"
XASH_PGO_DIR="${XASH_PGO_DIR:-$(pwd)/build/pgo-i73770}"

XASH_OPT_CFLAGS="-O3 -march=ivybridge -mtune=ivybridge -mfpmath=sse -pipe -pthread -fno-plt -fomit-frame-pointer -fstrict-aliasing -fno-semantic-interposition -fno-math-errno -fno-trapping-math -DNDEBUG -DXASH_I73770_ENGINE_PATCH=1 -DXASH_PARTICLE_FPS_PATCH=1"
XASH_OPT_LINKFLAGS="-pthread -Wl,-O1 -Wl,--as-needed -fno-plt"

if [ "$XASH_FAST_MATH" = "1" ]; then
  XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -ffast-math"
fi

if [ "$XASH_LTO" = "1" ]; then
  XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -flto=auto"
  XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -flto=auto"
fi

mkdir -p "$XASH_PGO_DIR"

case "$XASH_PGO" in
  gen|generate)
    XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -fprofile-generate=$XASH_PGO_DIR"
    XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -fprofile-generate=$XASH_PGO_DIR"
    ;;
  use)
    XASH_OPT_CFLAGS="$XASH_OPT_CFLAGS -fprofile-use=$XASH_PGO_DIR -fprofile-correction"
    XASH_OPT_LINKFLAGS="$XASH_OPT_LINKFLAGS -fprofile-use=$XASH_PGO_DIR"
    ;;
  off|"")
    ;;
  *)
    echo "[!] XASH_PGO inválido: $XASH_PGO"
    echo "    Use: XASH_PGO=off | gen | use"
    exit 1
    ;;
esac

echo "[*] libcurl libdir:    $(pkg-config --variable=libdir libcurl)"
echo "[*] libarchive libdir: $(pkg-config --variable=libdir libarchive)"
echo "[*] DL CFLAGS: $XASH_DL_CFLAGS"
echo "[*] DL LIBS:   $XASH_DL_LIBS"
echo "[*] OPT CFLAGS: $XASH_OPT_CFLAGS"
echo "[*] OPT LDFLAGS: $XASH_OPT_LINKFLAGS $XASH_DL_LIBS"
echo "[*] LTO=$XASH_LTO FAST_MATH=$XASH_FAST_MATH PGO=$XASH_PGO"
echo

CFLAGS="${CFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
CXXFLAGS="${CXXFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
LDFLAGS="${LDFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
./waf configure --disable-werror

CFLAGS="${CFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
CXXFLAGS="${CXXFLAGS:-} $XASH_OPT_CFLAGS $XASH_DL_CFLAGS" \
LINKFLAGS="${LINKFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
LDFLAGS="${LDFLAGS:-} $XASH_OPT_LINKFLAGS $XASH_DL_LIBS" \
./waf build -j$(nproc)
'''


def doc_text() -> str:
    return r'''# XASH Particle FPS Patch

Patch para reduzir quedas de FPS causadas por explosões/efeitos aglomerados.

## Arquivos adicionados

- `engine/common/xash_particle_budget.c`
- `engine/common/xash_particle_budget.h`
- `engine/common/xash_particle_job.c`
- `engine/common/xash_particle_job.h`

Como ficam em `engine/common/*.c`, entram automaticamente no build da engine.

## Controle por variável de ambiente

```bash
export XASH_PARTICLE_BUDGET=1
export XASH_PARTICLE_SCALE=0.75
export XASH_PARTICLE_BURST_MAX=384
export XASH_PARTICLE_WINDOW_MAX=900
export XASH_PARTICLE_VERBOSE=1
```

Sugestões:

- Visual mais fiel, menos ganho: `XASH_PARTICLE_SCALE=0.85`
- Equilíbrio: `XASH_PARTICLE_SCALE=0.75`
- Máximo FPS em explosões: `XASH_PARTICLE_SCALE=0.60`

## Job system

O arquivo `xash_particle_job.c` compila um helper pthread. Ele fica desligado por padrão.

```bash
export XASH_PARTICLE_JOB=1
export XASH_PARTICLE_WORKERS=3
```

## Build

```bash
./build_xash_32_curl_archive.sh
```

## PGO

```bash
XASH_PGO=gen ./build_xash_32_curl_archive.sh
# rode um mapa com explosões/partículas por alguns minutos
XASH_PGO=use ./build_xash_32_curl_archive.sh
```

## Verificação

```bash
grep -R "xash_particle_budget.c" build/compile_commands.json
grep -R "XASH_PARTICLE_FPS_PATCH" engine/client/cl_tent.c ref/gl/gl_rpart.c
```

## Teste runtime

```bash
XASH_PARTICLE_VERBOSE=1 XASH_PARTICLE_SCALE=0.75 ./build/game_launch/xash3d -dev 3 -console
```

## Reverter

```bash
python3 patch_particles.py --revert-last
```
'''


def insert_include_once(text: str, include_line: str) -> str:
    if include_line in text:
        return text
    lines = text.splitlines(True)
    last_inc = -1
    for i, line in enumerate(lines[:120]):
        if line.lstrip().startswith("#include"):
            last_inc = i
    if last_inc >= 0:
        lines.insert(last_inc + 1, include_line + "\n")
        return "".join(lines)
    return include_line + "\n" + text


def patch_after_once(text: str, marker: str, regex: str, insertion: str) -> tuple[str, bool]:
    if marker in text:
        return text, False
    m = re.search(regex, text, flags=re.S)
    if not m:
        return text, False
    pos = m.end()
    return text[:pos] + insertion + text[pos:], True


def patch_cl_tent(ctx: PatchCtx) -> None:
    path = ctx.root / "engine" / "client" / "cl_tent.c"
    if not path.exists():
        ctx.failures.append("engine/client/cl_tent.c não encontrado")
        return

    text = ctx.read(path)
    old = text

    text = insert_include_once(text, '#include "xash_particle_budget.h"')

    text, ok1 = patch_after_once(
        text,
        "XASH_PARTICLE_FPS_PATCH_R_FizzEffect",
        r"(void\s+GAME_EXPORT\s+R_FizzEffect\s*\([^)]*int\s+density\s*\)\s*\{.*?if\s*\(\s*!\s*ent\s*\|\|.*?\)\s*return\s*;)",
        "\n\t/* XASH_PARTICLE_FPS_PATCH_R_FizzEffect */\n"
        "\tdensity = XASH_ParticleClampDensity( \"R_FizzEffect\", density );\n"
        "\tif( density <= 0 ) return;\n"
    )

    text, ok2 = patch_after_once(
        text,
        "XASH_PARTICLE_FPS_PATCH_R_Bubbles",
        r"(void\s+GAME_EXPORT\s+R_Bubbles\s*\([^)]*int\s+count\s*,\s*float\s+speed\s*\)\s*\{.*?if\s*\(\s*\(\s*mod\s*=\s*CL_ModelHandle\s*\(\s*modelIndex\s*\)\s*\)\s*==\s*NULL\s*\)\s*return\s*;)",
        "\n\t/* XASH_PARTICLE_FPS_PATCH_R_Bubbles */\n"
        "\tcount = XASH_ParticleClampCount( \"R_Bubbles\", count );\n"
        "\tif( count <= 0 ) return;\n"
    )

    text, ok3 = patch_after_once(
        text,
        "XASH_PARTICLE_FPS_PATCH_R_BubbleTrail",
        r"(void\s+GAME_EXPORT\s+R_BubbleTrail\s*\([^)]*int\s+count\s*,\s*float\s+speed\s*\)\s*\{.*?if\s*\(\s*\(\s*mod\s*=\s*CL_ModelHandle\s*\(\s*modelIndex\s*\)\s*\)\s*==\s*NULL\s*\)\s*return\s*;)",
        "\n\t/* XASH_PARTICLE_FPS_PATCH_R_BubbleTrail */\n"
        "\tcount = XASH_ParticleClampCount( \"R_BubbleTrail\", count );\n"
        "\tif( count <= 0 ) return;\n"
    )

    text, ok4 = patch_after_once(
        text,
        "XASH_PARTICLE_FPS_PATCH_R_BreakModel",
        r"(void\s+GAME_EXPORT\s+R_BreakModel\s*\([^)]*int\s+count[^)]*\)\s*\{)",
        "\n\t/* XASH_PARTICLE_FPS_PATCH_R_BreakModel */\n"
        "\tcount = XASH_ParticleClampCount( \"R_BreakModel\", count );\n"
        "\tif( count <= 0 ) return;\n"
    )

    if not ok1:
        ctx.notes.append("não consegui aplicar clamp em R_FizzEffect; assinatura local pode ter mudado")
    if not ok2:
        ctx.notes.append("não consegui aplicar clamp em R_Bubbles; assinatura local pode ter mudado")
    if not ok3:
        ctx.notes.append("não consegui aplicar clamp em R_BubbleTrail; assinatura local pode ter mudado")
    if not ok4:
        ctx.notes.append("não consegui aplicar clamp em R_BreakModel; pode não existir nesse fork")

    if text != old:
        ctx.patch_text(path, text)
    else:
        ctx.skipped.append("engine/client/cl_tent.c")


def patch_gl_rpart(ctx: PatchCtx) -> None:
    path = ctx.root / "ref" / "gl" / "gl_rpart.c"
    if not path.exists():
        ctx.notes.append("ref/gl/gl_rpart.c não encontrado; pulando patch do renderer de partículas")
        return

    text = ctx.read(path)
    old = text

    text = insert_include_once(text, '#include "xash_particle_budget.h"')

    targets = [
        ("R_RunParticleEffect", r"(void\s+R_RunParticleEffect\s*\([^)]*int\s+count\s*\)\s*\{)"),
        ("R_RunParticleEffect2", r"(void\s+R_RunParticleEffect2\s*\([^)]*int\s+count[^)]*\)\s*\{)"),
        ("R_EntityParticles", r"(void\s+R_EntityParticles\s*\([^)]*int\s+count[^)]*\)\s*\{)"),
        ("R_LavaSplashCount", r"(void\s+R_LavaSplash\s*\([^)]*int\s+count[^)]*\)\s*\{)"),
    ]

    for name, rx in targets:
        marker = f"XASH_PARTICLE_FPS_PATCH_{name}"
        if marker in text:
            continue
        text, ok = patch_after_once(
            text,
            marker,
            rx,
            f"\n\t/* {marker} */\n\tcount = XASH_ParticleClampCount( \"{name}\", count );\n\tif( count <= 0 ) return;\n"
        )
        if not ok:
            ctx.notes.append(f"não consegui aplicar clamp em {name} no ref/gl/gl_rpart.c")

    if "XASH_PARTICLE_FPS_PATCH_PRAGMA" not in text:
        pragma = (
            "/* XASH_PARTICLE_FPS_PATCH_PRAGMA */\n"
            "#if defined(XASH_PARTICLE_FPS_PATCH) && (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__) && !defined(__clang__)\n"
            "#pragma GCC optimize (\"O3,omit-frame-pointer\")\n"
            "#pragma GCC target (\"sse4.1,sse4.2,avx\")\n"
            "#endif\n\n"
        )
        text = pragma + text

    if text != old:
        ctx.patch_text(path, text)
    else:
        ctx.skipped.append("ref/gl/gl_rpart.c")


def write_files(ctx: PatchCtx) -> None:
    ctx.write(ctx.root / "engine" / "common" / "xash_particle_budget.h", budget_h())
    ctx.write(ctx.root / "engine" / "common" / "xash_particle_budget.c", budget_c())
    ctx.write(ctx.root / "engine" / "common" / "xash_particle_job.h", job_h())
    ctx.write(ctx.root / "engine" / "common" / "xash_particle_job.c", job_c())
    ctx.write(ctx.root / "build_xash_32_curl_archive.sh", build_script(), executable=True)
    ctx.write(ctx.root / "Documentation" / "PARTICLE_FPS_PATCH.md", doc_text())


def write_report(ctx: PatchCtx) -> None:
    if ctx.dry_run:
        return

    ctx.state.mkdir(parents=True, exist_ok=True)
    (ctx.state / "last_manifest.txt").write_text("\n".join(ctx.changed) + "\n", encoding="utf-8")

    lines = []
    lines.append("# XASH Particle FPS Patch Report\n")
    lines.append(f"Root: `{ctx.root}`\n")
    lines.append(f"Backup: `{ctx.backup_dir}`\n")
    lines.append("\n## Alterados/criados\n")
    lines += [f"- `{x}`" for x in ctx.changed] or ["- nenhum"]
    lines.append("\n## Ignorados\n")
    lines += [f"- `{x}`" for x in ctx.skipped] or ["- nenhum"]
    lines.append("\n## Avisos\n")
    lines += [f"- {x}" for x in ctx.notes] or ["- nenhum"]
    lines.append("\n## Falhas\n")
    lines += [f"- {x}" for x in ctx.failures] or ["- nenhuma"]
    lines.append("\n## Build\n")
    lines.append("```bash")
    lines.append("./build_xash_32_curl_archive.sh")
    lines.append("```")
    lines.append("\n## Teste recomendado\n")
    lines.append("```bash")
    lines.append("XASH_PARTICLE_VERBOSE=1 XASH_PARTICLE_SCALE=0.75 ./build/game_launch/xash3d -dev 3 -console")
    lines.append("```")

    (ctx.state / "last_report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def revert_last(root: Path) -> None:
    backups = root / ".xash_particle_patch" / "backups"
    if not backups.exists():
        die("não há backups em .xash_particle_patch/backups")

    dirs = sorted([p for p in backups.iterdir() if p.is_dir()])
    if not dirs:
        die("não há backups disponíveis")

    last = dirs[-1]
    print(f"[xash-particles] revertendo backup: {last}")

    for src in last.rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to(last)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        print(f"  restaurado: {rel}")

    print("[xash-particles] revert concluído.")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="raiz do repo; padrão: diretório atual")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--revert-last", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()

    if not is_repo_root(root):
        die(
            "rode de dentro da raiz do XASH3D-ENCHANCED:\n"
            "  cd /home/gullin/claude/XASH3D-ENCHANCED/\n"
            "  python3 patch_particles.py"
        )

    if args.revert_last:
        revert_last(root)
        return 0

    ctx = PatchCtx(root, dry_run=args.dry_run)

    print("[xash-particles] aplicando patch de FPS para partículas...")
    print(f"[xash-particles] root: {root}")

    write_files(ctx)
    patch_cl_tent(ctx)
    patch_gl_rpart(ctx)
    write_report(ctx)

    print("\n[xash-particles] concluído.")
    print("\nAlterados/criados:")
    for x in ctx.changed:
        print("  +", x)
    if not ctx.changed:
        print("  nenhum")

    if ctx.skipped:
        print("\nIgnorados:")
        for x in ctx.skipped[:40]:
            print("  =", x)
        if len(ctx.skipped) > 40:
            print(f"  ... mais {len(ctx.skipped) - 40}")

    if ctx.notes:
        print("\nAvisos:")
        for x in ctx.notes:
            print("  !", x)

    if ctx.failures:
        print("\nFalhas:")
        for x in ctx.failures:
            print("  x", x)

    if not ctx.dry_run:
        print("\nBackup:")
        print(f"  {ctx.backup_dir}")
        print("\nCompile:")
        print("  ./build_xash_32_curl_archive.sh")
        print("\nVerifique se entrou no build:")
        print("  grep -R \"xash_particle_budget.c\" build/compile_commands.json")
        print("\nTeste:")
        print("  XASH_PARTICLE_VERBOSE=1 XASH_PARTICLE_SCALE=0.75 ./build/game_launch/xash3d -dev 3 -console")
        print("\nAjustes úteis:")
        print("  XASH_PARTICLE_SCALE=0.85  # mais visual")
        print("  XASH_PARTICLE_SCALE=0.60  # mais FPS")
        print("  XASH_PARTICLE_BURST_MAX=256")
        print("  XASH_PARTICLE_WINDOW_MAX=700")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
