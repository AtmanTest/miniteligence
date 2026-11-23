/* historique de conversation — teste sur PC (tests/unit) et sur la carte.
   Header seul : la meme implementation tourne dans les deux cas. */
#ifndef HISTORIQUE_H
#define HISTORIQUE_H

#include <string.h>
#include <stdio.h>

#define HIST_MAX      32   /* nombre de tours gardes (2x plus qu'avant : le fil tient) */
#define HIST_TXT_SZ  512   /* octets par tour */
#define HIST_QUI_SZ    8

typedef struct {
    char qui[HIST_MAX][HIST_QUI_SZ];   /* "MOI" (Taz) ou "MINO" */
    char txt[HIST_MAX][HIST_TXT_SZ];
    int  n;
} hist_t;

static inline void hist_vide(hist_t *h) { h->n = 0; }

/* ajoute un tour ; le plus ancien sort quand c'est plein.
   ignore un texte vide. renvoie le nombre de tours apres ajout. */
static inline int hist_ajoute(hist_t *h, const char *qui, const char *texte)
{
    if (!h || !texte || !*texte) return h ? h->n : 0;
    if (h->n == HIST_MAX) {
        memmove(h->qui[0], h->qui[1], sizeof(h->qui[0]) * (HIST_MAX - 1));
        memmove(h->txt[0], h->txt[1], sizeof(h->txt[0]) * (HIST_MAX - 1));
        h->n--;
    }
    snprintf(h->qui[h->n], HIST_QUI_SZ, "%s", qui ? qui : "MOI");
    snprintf(h->txt[h->n], HIST_TXT_SZ, "%.*s", HIST_TXT_SZ - 1, texte);
    h->n++;
    return h->n;
}

/* l'assistant repond-il a ce tour ? (utilise pour le role OpenAI) */
static inline int hist_est_mino(const hist_t *h, int i)
{
    return h && i >= 0 && i < h->n && strcmp(h->qui[i], "MINO") == 0;
}

#endif
