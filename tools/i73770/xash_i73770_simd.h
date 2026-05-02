#ifndef XASH_I73770_SIMD_H
#define XASH_I73770_SIMD_H

/*
 * Helpers SIMD opcionais para futuras integrações manuais.
 * Não altera a engine sozinho.
 */

#include <stddef.h>

#if defined(__SSE__) || defined(__SSE2__) || defined(__AVX__)
#include <xmmintrin.h>
#endif

#if defined(__AVX__)
#include <immintrin.h>
#endif

static inline void xash_i73770_vec3_add_scalar(float *x, float *y, float *z, float ax, float ay, float az, size_t n)
{
    size_t i = 0;

#if defined(__AVX__)
    __m256 vx = _mm256_set1_ps(ax);
    __m256 vy = _mm256_set1_ps(ay);
    __m256 vz = _mm256_set1_ps(az);

    for (; i + 8 <= n; i += 8)
    {
        _mm256_storeu_ps(x + i, _mm256_add_ps(_mm256_loadu_ps(x + i), vx));
        _mm256_storeu_ps(y + i, _mm256_add_ps(_mm256_loadu_ps(y + i), vy));
        _mm256_storeu_ps(z + i, _mm256_add_ps(_mm256_loadu_ps(z + i), vz));
    }
#elif defined(__SSE__)
    __m128 vx = _mm_set1_ps(ax);
    __m128 vy = _mm_set1_ps(ay);
    __m128 vz = _mm_set1_ps(az);

    for (; i + 4 <= n; i += 4)
    {
        _mm_storeu_ps(x + i, _mm_add_ps(_mm_loadu_ps(x + i), vx));
        _mm_storeu_ps(y + i, _mm_add_ps(_mm_loadu_ps(y + i), vy));
        _mm_storeu_ps(z + i, _mm_add_ps(_mm_loadu_ps(z + i), vz));
    }
#endif

    for (; i < n; ++i)
    {
        x[i] += ax;
        y[i] += ay;
        z[i] += az;
    }
}

#endif /* XASH_I73770_SIMD_H */
