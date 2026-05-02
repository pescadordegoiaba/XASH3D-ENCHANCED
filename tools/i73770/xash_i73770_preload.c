/*
 * libxash_i73770_preload.so
 *
 * Runtime opcional via LD_PRELOAD.
 * Ele roda antes da engine e aplica ajustes seguros:
 *   - FTZ/DAZ no MXCSR para evitar custo alto de denormal floats;
 *   - mallopt em glibc para reduzir arenas excessivas;
 *   - imprime informações úteis quando XASH_I73770_VERBOSE=1.
 *
 * Desativar:
 *   XASH_I73770_PRELOAD=0 ./scripts/xash_i73770_run.sh -- -game cstrike
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <unistd.h>
#endif

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#if defined(__SSE__) || defined(__SSE2__) || defined(__AVX__)
#include <xmmintrin.h>
#endif

static int xash_i73770_env_on(const char *name, int defval)
{
    const char *v = getenv(name);
    if (!v || !*v) return defval;
    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "FALSE") ||
        !strcmp(v, "no") || !strcmp(v, "NO") || !strcmp(v, "off") || !strcmp(v, "OFF"))
        return 0;
    return 1;
}

static long xash_i73770_cpu_count(void)
{
#if defined(__linux__)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    return n;
#else
    return 1;
#endif
}

static void xash_i73770_enable_fast_float_mode(void)
{
#if defined(__SSE__) || defined(__SSE2__) || defined(__AVX__)
    unsigned int mxcsr = _mm_getcsr();

    /* bit 15 = FTZ, bit 6 = DAZ */
    mxcsr |= (1u << 15);
    mxcsr |= (1u << 6);

    _mm_setcsr(mxcsr);
#endif
}

__attribute__((constructor))
static void xash_i73770_preload_init(void)
{
    if (!xash_i73770_env_on("XASH_I73770_PRELOAD", 1))
        return;

    xash_i73770_enable_fast_float_mode();

#if defined(__GLIBC__)
    if (xash_i73770_env_on("XASH_I73770_MALLOC_TUNE", 1))
    {
#ifdef M_ARENA_MAX
        mallopt(M_ARENA_MAX, 2);
#endif
#ifdef M_MMAP_THRESHOLD
        mallopt(M_MMAP_THRESHOLD, 128 * 1024);
#endif
    }
#endif

    if (xash_i73770_env_on("XASH_I73770_VERBOSE", 0))
    {
        fprintf(stderr,
            "[xash-i73770] preload ativo | cpus=%ld | FTZ/DAZ=on | malloc_tune=%s\n",
            xash_i73770_cpu_count(),
            xash_i73770_env_on("XASH_I73770_MALLOC_TUNE", 1) ? "on" : "off");
    }
}
