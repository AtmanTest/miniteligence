/*  mt_ui.cpp — 6 écrans, 5x10, 1 bit. Rien en dessous de 10 px de haut. */
#include "mt_ui.h"
#include "art.h"
#include "mt_tests_stats.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define FX0 4
#define FX1 195
#define FOOT 182

static void plein(int x0, int y0, int x1, int y1, int noir)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) a_px(x, y, noir);
}

static void cadre(int x0, int y0, int x1, int y1, int t)
{
    plein(x0, y0, x1, y0 + t - 1, 1);
    plein(x0, y1 - t + 1, x1, y1, 1);
    plein(x0, y0, x0 + t - 1, y1, 1);
    plein(x1 - t + 1, y0, x1, y1, 1);
}

static void t5(int x, int y, const char *s)  { if (s) a_text5c(x, y, 1, s, 1); }
static void t5w(int x, int y, const char *s) { if (s) a_text5c(x, y, 1, s, 0); }
static void t5b(int x, int y, const char *s) { if (s) a_text5c(x, y, 2, s, 1); }
static int  largeur(const char *s) { return s ? a_text5_w(1, s) : 0; }
static void t5d(int xd, int y, const char *s) { if (s) t5(xd - largeur(s), y, s); }
static void t5centre(int cx, int y, const char *s) { if (s) t5(cx - largeur(s) / 2, y, s); }

static void elide(char *dst, size_t cap, int maxw, const char *src)
{
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%s", src ? src : "");
    src = tmp;
    size_t maxc = (size_t)(maxw / 6);
    if (maxc >= cap) maxc = cap - 1;
    size_t n = strlen(src);
    if (n <= maxc) { snprintf(dst, cap, "%s", src); return; }
    if (maxc < 4) { dst[0] = 0; return; }
    memcpy(dst, src, maxc - 2);
    dst[maxc - 2] = '.'; dst[maxc - 1] = '.'; dst[maxc] = 0;
}

/* texte sur n lignes, coupé aux espaces */
static int paragraphe(int x, int y, int maxw, const char *t, int lignes, int interligne)
{
    char buf[300];
    snprintf(buf, sizeof(buf), "%s", t ? t : "");
    char *p = buf;
    int maxc = maxw / 6;
    for (int l = 0; l < lignes && *p; l++) {
        int n = (int)strlen(p);
        int cut = n;
        if (n > maxc) {
            cut = maxc;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut <= 0) cut = maxc;
        }
        char tmp[300];
        memcpy(tmp, p, (size_t)cut);
        tmp[cut] = 0;
        t5(x, y + l * interligne, tmp);
        p += cut;
        while (*p == ' ') p++;
    }
    return y + lignes * interligne;
}

static void entete(const mt_vue_t *v, const char *titre)
{
    plein(0, 0, 199, 13, 1);
    t5w(5, 2, titre);
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", v->ecran + 1, MT_ECRAN_NB);
    t5w(181 - largeur(b), 2, b);
    if (v->wifi) plein(187, 4, 195, 10, 0);
    else         cadre(187, 4, 195, 10, 1);
}

static void pied(const mt_vue_t *v)
{
    if (v->message) {
        plein(0, 176, 199, 196, 1);
        char b[52];
        elide(b, sizeof(b), 186, v->message);
        t5w(6, 181, b);
        return;
    }
    char a[34];
    elide(a, sizeof(a), 126, mt_ecran_aide(v->ecran));
    t5(FX0, FOOT, a);
    t5d(198, FOOT, "B1L=SUIV");
}

static void sel_bar(int y, int h, bool sel) { if (sel) plein(0, y, 199, y + h - 1, 1); }

/* ------------------------------------------------------------------ */
/* 1. briefing                                                        */
/* ------------------------------------------------------------------ */
static void p_brief(const mt_vue_t *v)
{
    const mt_store_t *s = v->store;
    char b[52];

    time_t t = (time_t)v->now;
    struct tm ti;
    localtime_r(&t, &ti);
    static const char *JOUR[] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
    if (v->heure_ok)
        snprintf(b, sizeof(b), "%s %02d/%02d  %02d:%02d", JOUR[ti.tm_wday],
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
    else
        snprintf(b, sizeof(b), "HEURE NON SYNCHRO");
    t5(6, 16, b);

    t5b(6, 30, "MINITELIGENCE");

    cadre(4, 54, 196, 92, 1);
    snprintf(b, sizeof(b), "%d NOTES", s->n);
    t5(9, 58, b);
    snprintf(b, sizeof(b), "%d A REVOIR", v->dues);
    t5(9, 72, b);
    if (v->dues > 0) t5d(186, 72, "B2X2 REVISER");
    else             t5d(186, 72, "A JOUR");

    /* la dernière note, courte */
    t5(6, 100, "DERNIERE NOTE");
    if (s->n) {
        const mt_note_t *n = &s->notes[s->n - 1];
        snprintf(b, sizeof(b), "[%s]", n->tag);
        t5(6, 112, b);
        char l[44];
        elide(l, sizeof(l), 186, n->txt);
        t5(6, 124, l);
    } else {
        t5(6, 112, "AUCUNE NOTE");
        t5(6, 124, "ENVOYEZ-EN UNE PAR LE WEB");
    }

    /* capteurs en une ligne */
    if (!isnan(v->temp_c))
        snprintf(b, sizeof(b), "%.1f C  %.0f%% HR", (double)v->temp_c, (double)v->hum);
    else
        snprintf(b, sizeof(b), "CAPTEURS : N.D.");
    t5(6, 142, b);

    sel_bar(156, 15, v->curseur == 0);
    a_text5c(8, 158, 1, v->dues ? "REVISER MAINTENANT" : "ECRIRE UNE NOTE",
             v->curseur == 0 ? 0 : 1);
}

/* ------------------------------------------------------------------ */
/* 2. mémoire                                                         */
/* ------------------------------------------------------------------ */
static void p_memoire(const mt_vue_t *v)
{
    const mt_store_t *s = v->store;
    char b[52];

    snprintf(b, sizeof(b), "%d NOTES  %d DUES", s->n, v->dues);
    t5(6, 16, b);

    if (s->n == 0) {
        t5centre(100, 70, "CARNET VIDE");
        t5centre(100, 86, "POSTEZ /note?t=...");
        return;
    }

    /* 3 notes par écran, les plus récentes d'abord */
    int base = v->curseur * 3;
    for (int k = 0; k < 3 && base + k < s->n; k++) {
        int idx = s->n - 1 - (base + k);
        if (idx < 0) break;
        const mt_note_t *n = &s->notes[idx];
        int y = 30 + k * 40;

        snprintf(b, sizeof(b), "[%s]%s", n->tag, n->due <= v->now ? "  A REVOIR" : "");
        t5(6, y, b);
        char l[46];
        elide(l, sizeof(l), 186, n->txt);
        t5(6, y + 12, l);
        elide(l, sizeof(l), 186, n->txt[0] ? n->txt : "");
        snprintf(b, sizeof(b), "J+%d  EASE %.1f  x%d", mt_jours(n->cree, v->now),
                 (double)n->ease, n->reps);
        t5(6, y + 24, b);
        if (k < 2) plein(FX0, y + 34, FX1, y + 34, 1);
    }

    int pages = (s->n + 2) / 3;
    snprintf(b, sizeof(b), "PAGE %d/%d", v->curseur + 1, pages);
    t5d(194, 166, b);
}

/* ------------------------------------------------------------------ */
/* 3. question                                                        */
/* ------------------------------------------------------------------ */
static void p_question(const mt_vue_t *v)
{
    char b[52];

    if (v->etat == MT_REFLEXION) {
        t5(6, 30, "JE CHERCHE DANS MES NOTES");
        for (int i = 0; i < 3; i++)
            plein(40 + i * 12, 52, 160 - i * 20, 58 + i * 12, 1);
        if (v->scores[0] > 0.0f) {
            snprintf(b, sizeof(b), "%d NOTES RETENUES", (int)v->scores[1]);
            t5(6, 110, b);
        }
        t5(6, 130, "PUIS JE DEMANDE AU MODELE");
        t5(6, 146, "REPONSE COURTE, SOURCEE");
        return;
    }

    if (v->etat == MT_ERREUR) {
        t5(6, 26, "PAS DE REPONSE");
        paragraphe(6, 44, 186, v->reponse ? v->reponse : "reseau indisponible", 4, 12);
        t5(6, 110, "LE CARNET RESTE UTILISABLE");
        t5(6, 124, "SANS RESEAU : RECHERCHE");
        t5(6, 138, "LOCALE SEULEMENT");
        return;
    }

    if (v->etat == MT_REPONSE && v->reponse) {
        t5(6, 18, "REPONSE");
        plein(FX0, 30, FX1, 30, 1);
        paragraphe(6, 38, 186, v->reponse, 9, 13);
        if (v->question && v->question[0]) {
            char q[40];
            snprintf(q, sizeof(q), "Q: %s", v->question);
            elide(b, sizeof(b), 186, q);
            t5(6, 166, b);
        }
        return;
    }

    t5centre(100, 50, "POSEZ UNE QUESTION");
    t5centre(100, 68, "DEPUIS LE NAVIGATEUR");
    cadre(10, 90, 190, 118, 1);
    t5(14, 94, "http://<ip>/ask?q=...");
    t5(14, 106, "OU /note?t=... POUR NOTER");
    t5(6, 134, "SANS RESEAU, ELLE CHERCHE");
    t5(6, 146, "DANS LES NOTES, SUR LA CARTE");
}

/* ------------------------------------------------------------------ */
/* 4. révision                                                        */
/* ------------------------------------------------------------------ */
static void p_revision(const mt_vue_t *v)
{
    const mt_store_t *s = v->store;
    char b[52];

    if (!s->n) {
        t5centre(100, 70, "AUCUNE NOTE A REVOIR");
        t5centre(100, 86, "ECRIVEZ-EN D'ABORD");
        return;
    }
    int idx = v->rev_idx >= 0 && v->rev_idx < s->n ? v->rev_idx : (s->n - 1);
    const mt_note_t *n = &s->notes[idx];

    snprintf(b, sizeof(b), "CARTE %d/%d  DUE", v->rev_n + 1, v->rev_total > 0 ? v->rev_total : 1);
    t5(6, 16, b);
    snprintf(b, sizeof(b), "[%s]  x%d  %d OUBLI%s", n->tag, n->reps, n->oublis,
             n->oublis > 1 ? "S" : "");
    t5(6, 30, b);

    plein(FX0, 44, FX1, 44, 1);
    paragraphe(6, 54, 186, n->txt, 6, 13);

    sel_bar(142, 15, false);
    t5w(8, 144, "B1 : JE SAVAIS");
    cadre(6, 160, 194, 174, 1);
    t5(10, 163, "B1X2 : A REVOIR");
}

/* ------------------------------------------------------------------ */
/* 5. capteurs                                                        */
/* ------------------------------------------------------------------ */
static void p_capteurs(const mt_vue_t *v)
{
    char b[52];
    struct { const char *t, *val; } L[5];
    char t1[16], t2[16], t3[16], t4[16], t5v[16];

    snprintf(t1, sizeof(t1), isnan(v->temp_c) ? "N.D." : "%.1f C", (double)v->temp_c);
    snprintf(t2, sizeof(t2), isnan(v->hum) ? "N.D." : "%.0f %%", (double)v->hum);
    snprintf(t3, sizeof(t3), v->bat_pct < 0 ? "N.D." : "%d %%", v->bat_pct);
    snprintf(t4, sizeof(t4), v->wifi ? "%d DBM" : "HORS LIGNE", v->rssi);
    snprintf(t5v, sizeof(t5v), v->sd ? "MONTE" : "ABSENTE");

    L[0].t = "TEMPERATURE"; L[0].val = t1;
    L[1].t = "HUMIDITE";    L[1].val = t2;
    L[2].t = "BATTERIE";    L[2].val = t3;
    L[3].t = "WIFI";        L[3].val = t4;
    L[4].t = "CARTE SD";    L[4].val = t5v;

    for (int i = 0; i < 5; i++) {
        int y = 20 + i * 16;
        sel_bar(y - 2, 15, v->curseur == i);
        int col = v->curseur == i ? 0 : 1;
        a_text5c(6, y, 1, L[i].t, col);
        a_text5c(194 - largeur(L[i].val), y, 1, L[i].val, col);
    }

    plein(FX0, 106, FX1, 106, 1);
    snprintf(b, sizeof(b), "HEAP %u KO  PSRAM %u MO", (unsigned)(v->heap / 1024),
             (unsigned)(v->psram / 1048576));
    t5(6, 112, b);
    snprintf(b, sizeof(b), "RENDU %d MS  ECRAN %d MS", v->rendu_ms, v->ecran_ms);
    t5(6, 124, b);
    t5(6, 142, "CAPTEURS I2C : SHTC3 0x70");
    t5(6, 154, "HORLOGE : PCF85063 0x51");
}

/* ------------------------------------------------------------------ */
/* 6. système                                                         */
/* ------------------------------------------------------------------ */
static void p_systeme(const mt_vue_t *v)
{
    char b[52];
    int total = MT_TESTS_UNIT + MT_TESTS_INTEG;

    snprintf(b, sizeof(b), "MINITELIGENCE " MT_VERSION);
    t5(6, 18, b);
    snprintf(b, sizeof(b), "%d VERIFICATIONS", total);
    t5(6, 32, b);
    t5b(6, 44, MT_TESTS_FAIL == 0 ? "0 ECHEC" : "ECHECS");

    plein(FX0, 68, FX1, 68, 1);
    t5(6, 74, "TF-IDF LOCAL + SM-2");
    t5(6, 86, "LLM : GROQ / DEEPSEEK");
    t5(6, 98, "MEMOIRE : /sdcard/mt.db");
    t5(6, 110, "API : /note /ask /etat /ecran");

    t5(6, 124, "EFFACER MES DONNEES : B1X2");
    t5(6, 136, "DEUX FOIS DE SUITE");
    t5(6, 154, "@PARISTZOU");
    t5(6, 166, "JUSTE POUR LES FILLES");
}

/* ------------------------------------------------------------------ */
/* table                                                              */
/* ------------------------------------------------------------------ */
static const char *TITRE[MT_ECRAN_NB] = {
    "BRIEFING", "MEMOIRE", "QUESTION", "REVISION", "CAPTEURS", "SYSTEME"
};

const char *mt_ecran_titre(int e)
{
    if (e < 0 || e >= MT_ECRAN_NB) return "?";
    return TITRE[e];
}

const char *mt_ecran_aide(int e)
{
    switch (e) {
    case MT_ECRAN_BRIEF:    return "B2X2 OUVRIR";
    case MT_ECRAN_MEMOIRE:  return "B2 PAGE  B1L RETOUR";
    case MT_ECRAN_QUESTION: return "B2X2 RELANCER";
    case MT_ECRAN_REVISION: return "B1 SAVOIR  B1X2 REVOIR";
    case MT_ECRAN_CAPTEURS: return "B2X2 MESURER";
    case MT_ECRAN_SYSTEME:  return "B1 EFFACER";
    default:                return "";
    }
}

int mt_ecran_items(int e, const mt_vue_t *v)
{
    switch (e) {
    case MT_ECRAN_MEMOIRE:  return (v->store->n + 2) / 3 > 0 ? (v->store->n + 2) / 3 : 1;
    case MT_ECRAN_CAPTEURS: return 5;
    case MT_ECRAN_REVISION: return 2;
    default:                return 1;
    }
}

void mt_ui_dessine(const mt_vue_t *v, uint8_t *fb)
{
    art_begin(fb, 0);
    art_clear();
    if (!v || !v->store) return;

    entete(v, mt_ecran_titre(v->ecran));
    switch (v->ecran) {
    case MT_ECRAN_BRIEF:    p_brief(v); break;
    case MT_ECRAN_MEMOIRE:  p_memoire(v); break;
    case MT_ECRAN_QUESTION: p_question(v); break;
    case MT_ECRAN_REVISION: p_revision(v); break;
    case MT_ECRAN_CAPTEURS: p_capteurs(v); break;
    case MT_ECRAN_SYSTEME:  p_systeme(v); break;
    default: t5centre(100, 90, "ECRAN INCONNU"); break;
    }
    pied(v);
}
