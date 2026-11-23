/*  check.h — le harnais de test maison (pas de dependance).
 *  Un test = une ligne de sortie. Le programme rend le nombre d'echecs.
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>
#include <math.h>

static int checks = 0, echecs = 0;

#define OK(cond, quoi) do { \
    checks++; \
    if (!(cond)) { echecs++; printf("  ECHEC  %s  (%s:%d)\n", quoi, __FILE__, __LINE__); } \
} while (0)

#define OKF(cond, quoi, ...) do { \
    checks++; \
    if (!(cond)) { echecs++; printf("  ECHEC  " quoi "  (%s:%d)\n", __VA_ARGS__, __FILE__, __LINE__); } \
} while (0)

#define PRES(x) do { \
    checks++; \
    if (!(x)) { echecs++; printf("  ECHEC  %s\n", #x); } \
} while (0)

#define PROCHE(a, b, eps, quoi) do { \
    checks++; \
    double _d = fabs((double)(a) - (double)(b)); \
    if (_d > (eps)) { echecs++; printf("  ECHEC  %s : %.4f != %.4f\n", quoi, (double)(a), (double)(b)); } \
} while (0)

#define FIN(nom) do { \
    printf("%-22s %3d verifications, %d echec(s)\n", nom, checks, echecs); \
    return echecs ? 1 : 0; \
} while (0)

#endif
