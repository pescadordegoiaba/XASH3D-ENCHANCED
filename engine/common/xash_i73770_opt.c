/*
 * XASH3D i7-3770 / Ivy Bridge optimization runtime.
 *
 * Este arquivo fica em engine/common/xash_i73770_opt.c porque o engine/wscript
 * compila engine/common/*.c automaticamente.
 *
 * Ativo por padrão:
 *   - FTZ/DAZ para reduzir stalls de float denormal;
 *   - ajuste leve de malloc arenas no glibc;
 *   - helpers de contador de CPU e timer.
 *
 * Controle por env:
 *   XASH_I73770_OPT=0        desativa o init automático
 *   XASH_I73770_VERBOSE=1    imprime diagnóstico
 *   XASH_I73770_MALLOC=0     desativa mallopt
 *
 * Ele NÃO muda física, rede ou protocolo.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xash_i73770_opt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
#include <unistd.h>
#include <sched.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

#if (defined(__i386__) || defined(__x86_64__)) && (defined(__SSE__) || defined(__SSE2__) || defined(__AVX__))
#include <xmmintrin.h>
#endif

static int XASH_I73770_EnvBool(const char *name, int defval)
{
	const char *v = getenv(name);
	if (!v || !*v) return defval;
	if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "FALSE") || !strcmp(v, "no") || !strcmp(v, "NO"))
		return 0;
	return 1;
}

int XASH_I73770_CPUCount(void)
{
#if defined(__linux__)
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1) return 1;
	if (n > 256) return 256;
	return (int)n;
#else
	return 1;
#endif
}

int XASH_I73770_RecommendedWorkerCount(void)
{
	int n = XASH_I73770_CPUCount();

	/*
	 * i7-3770 = 4C/8T.
	 * Para tarefas futuras da engine, recomenda-se deixar 1 thread lógica livre.
	 */
	if (n >= 8) return 7;
	if (n >= 4) return n - 1;
	if (n >= 2) return 1;
	return 0;
}

double XASH_I73770_TimeSeconds(void)
{
#if defined(CLOCK_MONOTONIC)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#else
	return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

void XASH_I73770_EnableFastFloat(void)
{
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__SSE__) || defined(__SSE2__) || defined(__AVX__))
	unsigned int mxcsr = _mm_getcsr();

	/*
	 * FTZ = Flush To Zero, bit 15.
	 * DAZ = Denormals Are Zero, bit 6.
	 * Ajuda em áudio, partículas e interpolação quando aparecem floats minúsculos.
	 */
	mxcsr |= (1u << 15);
	mxcsr |= (1u << 6);

	_mm_setcsr(mxcsr);
#endif
}

void XASH_I73770_OptInit(void)
{
	if (!XASH_I73770_EnvBool("XASH_I73770_OPT", 1))
		return;

	XASH_I73770_EnableFastFloat();

#if defined(__linux__) && defined(__GLIBC__)
	if (XASH_I73770_EnvBool("XASH_I73770_MALLOC", 1))
	{
#ifdef M_ARENA_MAX
		mallopt(M_ARENA_MAX, 2);
#endif
#ifdef M_MMAP_THRESHOLD
		mallopt(M_MMAP_THRESHOLD, 131072);
#endif
	}
#endif

	if (XASH_I73770_EnvBool("XASH_I73770_VERBOSE", 0))
	{
		fprintf(stderr,
			"[xash-i73770] engine patch ativo: cpus=%d workers_recomendados=%d FTZ/DAZ=on\n",
			XASH_I73770_CPUCount(),
			XASH_I73770_RecommendedWorkerCount());
	}
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void XASH_I73770_Constructor(void)
{
	XASH_I73770_OptInit();
}
#endif
