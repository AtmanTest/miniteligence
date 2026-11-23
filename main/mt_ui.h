/*  mt_ui.h — les 6 écrans de MiniTelligence, dessinés en 1 bit. */
#ifndef MT_UI_H
#define MT_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "mt_model.h"

#define MT_ECRAN_W 200
#define MT_ECRAN_H 200

typedef enum {
    MT_ECRAN_BRIEF = 0,
    MT_ECRAN_MEMOIRE,
    MT_ECRAN_QUESTION,
    MT_ECRAN_REVISION,
    MT_ECRAN_CAPTEURS,
    MT_ECRAN_SYSTEME,
    MT_ECRAN_NB
} mt_ecran_t;

typedef enum { MT_REPOS = 0, MT_REFLEXION, MT_REPONSE, MT_ERREUR } mt_etat_t;

typedef struct {
    int      ecran, curseur;
    const mt_store_t *store;
    int64_t  now;
    bool     heure_ok, wifi, sd;
    int      rssi;
    int      bat_pct;            /* -1 = inconnu */
    float    temp_c, hum;        /* NAN si capteur absent */
    int      dues;
    int      rendu_ms, ecran_ms;
    uint32_t heap, psram;
    const char *message;

    /* dialogue */
    mt_etat_t etat;
    const char *question;
    const char *reponse;
    float    scores[4];          /* ce que la recherche a retenu */

    /* révision */
    int      rev_idx;            /* note en cours, -1 = aucune */
    int      rev_n, rev_total;
    bool     rev_visible;        /* texte dévoilé ? */
} mt_vue_t;

void mt_ui_dessine(const mt_vue_t *v, uint8_t *fb);
const char *mt_ecran_titre(int ecran);
const char *mt_ecran_aide(int ecran);
int mt_ecran_items(int ecran, const mt_vue_t *v);

#endif
