#ifndef XASH_I73770_OPT_H
#define XASH_I73770_OPT_H

/*
 * Helpers de otimização para builds x86/i7-3770.
 * Este header fica em engine/common porque engine/wscript compila common/*.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

void XASH_I73770_OptInit(void);
void XASH_I73770_EnableFastFloat(void);
int  XASH_I73770_CPUCount(void);
int  XASH_I73770_RecommendedWorkerCount(void);
double XASH_I73770_TimeSeconds(void);

#ifdef __cplusplus
}
#endif

#endif /* XASH_I73770_OPT_H */
