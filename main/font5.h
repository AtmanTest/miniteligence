/* font5.h -- police 5x10 du lecteur (genere par tools/gen_font5.py)
 * NE PAS EDITER A LA MAIN : relancer le generateur. */
#ifndef FONT5_H_INCLUDED
#define FONT5_H_INCLUDED
#include <stdint.h>

#define FONT5_LARGEUR  5
#define FONT5_HAUTEUR  10
#define FONT5_PAS      6      /* avance d'un caractere */
#define FONT5_INTERLIGNE 11   /* pas d'une ligne a l'autre */
#define FONT5_N 123

extern const uint8_t FONT5[FONT5_N][FONT5_HAUTEUR];
uint8_t font5_code(uint32_t cp);   /* 0 = inconnu */
int     font5_cols(int largeur_px);

#endif
