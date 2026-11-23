/*  mt_model.cpp — mémoire, TF-IDF, SM-2. Pur C++ : tourne aussi sur le PC. */
#include "mt_model.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

/* ================================================================== */
/* texte                                                              */
/* ================================================================== */
void mt_copie(char *dst, size_t cap, const char *src)
{
    if (!dst || !cap) return;
    size_t i = 0;
    for (const char *p = src ? src : ""; *p && i + 1 < cap; p++) dst[i++] = *p;
    dst[i] = 0;
}

/* UTF-8 -> ASCII : les accents deviennent leur lettre, le reste tombe.
   Table explicite : une erreur ici et le TF-IDF compare des mots faux. */
void mt_ascii(const char *in, char *out, size_t cap)
{
    static const struct { unsigned char d; char r; } T[] = {
        { 0xA0, 'a' }, { 0xA1, 'a' }, { 0xA2, 'a' }, { 0xA3, 'a' }, { 0xA4, 'a' },  /* à á â ã ä */
        { 0xA7, 'c' }, { 0x87, 'c' },                                              /* ç Ç       */
        { 0xA8, 'e' }, { 0xA9, 'e' }, { 0xAA, 'e' }, { 0xAB, 'e' },                /* è é ê ë   */
        { 0x89, 'e' }, { 0x8A, 'e' }, { 0x8B, 'e' },
        { 0xAE, 'i' }, { 0xAF, 'i' }, { 0x8E, 'i' }, { 0x8F, 'i' },                /* î ï Î Ï   */
        { 0xB4, 'o' }, { 0xB6, 'o' }, { 0xB8, 'o' }, { 0x94, 'o' }, { 0x96, 'o' }, /* ô ö ø     */
        { 0xB9, 'u' }, { 0xBB, 'u' }, { 0x9B, 'u' }, { 0x99, 'u' },                /* ù û Ù Û   */
        { 0xB1, 'n' }, { 0xA6, 'a' },                                              /* ñ æ       */
        { 0x9C, 'o' },                                                             /* œ         */
    };
    size_t o = 0;
    if (!out || !cap) return;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && o + 1 < cap; p++) {
        unsigned char c = *p;
        if (c < 0x80) {
            out[o++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            continue;
        }
        if ((c == 0xC3 || c == 0xC5) && p[1]) {
            for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
                if (T[i].d == p[1]) { out[o++] = T[i].r; break; }
            }
            p++;
        }
    }
    out[o] = 0;
}

static const char *VIDES[] = {
    "le", "les", "la", "un", "une", "des", "de", "du", "au", "aux", "et", "ou",
    "ce", "cet", "cette", "ces", "mon", "ma", "mes", "ton", "ta", "tes", "son",
    "sa", "ses", "pour", "sur", "dans", "avec", "pas", "plus", "est", "sont",
    "que", "qui", "quoi", "dont", "il", "elle", "ils", "elles", "je", "tu",
    "nous", "vous", "on", "se", "ne", "en", "y", "a", "ai", "as", "the", "and"
};

static bool mot_vide(const char *m)
{
    for (size_t i = 0; i < sizeof(VIDES) / sizeof(VIDES[0]); i++)
        if (strcmp(m, VIDES[i]) == 0) return true;
    return false;
}

int mt_tokens(const char *txt, char out[][MT_TOKEN_MAX + 1], int max)
{
    char a[MT_TXT_L * 2];
    mt_ascii(txt, a, sizeof(a));
    int n = 0;
    size_t i = 0;
    while (a[i] && n < max) {
        while (a[i] && !isalnum((unsigned char)a[i])) i++;
        size_t d = i;
        while (a[i] && isalnum((unsigned char)a[i])) i++;
        size_t len = i - d;
        if (len < 2) continue;                 /* "a", "y" : aucun sens */
        if (len > MT_TOKEN_MAX) len = MT_TOKEN_MAX;
        memcpy(out[n], a + d, len);
        out[n][len] = 0;
        mt_radical(out[n]);
        if (!mot_vide(out[n])) n++;
    }
    return n;
}

/* pluriels simples : voitures -> voiture, journaux -> journal */
void mt_radical(char *mot)
{
    size_t n = strlen(mot);
    if (n > 5 && strcmp(mot + n - 3, "aux") == 0) { strcpy(mot + n - 3, "al"); return; }
    if (n > 4 && (mot[n - 1] == 's' || mot[n - 1] == 'x')) mot[n - 1] = 0;
}

/* ================================================================== */
/* notes                                                              */
/* ================================================================== */
uint32_t mt_hash(const char *txt)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)(txt ? txt : ""); *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

void mt_store_init(mt_store_t *s, uint32_t graine)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->graine = graine ? graine : 1u;
}

void mt_etiquette(const char *txt, char *out, size_t cap)
{
    char mots[8][MT_TOKEN_MAX + 1];
    int n = mt_tokens(txt, mots, 8);
    snprintf(out, cap, "%s", n ? mots[0] : "NOTE");
    for (size_t i = 0; out[i]; i++) out[i] = (char)toupper((unsigned char)out[i]);
}

int mt_ajoute(mt_store_t *s, const char *txt, const char *tag, int64_t now)
{
    if (!s || !txt || !txt[0]) return -1;
    if (s->n >= MT_NOTES_MAX) return -2;                 /* plein : à trier */
    uint32_t h = mt_hash(txt);
    if (mt_trouve_hash(s, h) >= 0) return -3;            /* déjà connu     */

    mt_note_t *n = &s->notes[s->n];
    memset(n, 0, sizeof(*n));
    mt_ascii(txt, n->txt, sizeof(n->txt));
    /* on garde la casse d'origine pour l'affichage */
    mt_copie(n->txt, sizeof(n->txt), txt);
    if (tag && tag[0]) mt_copie(n->tag, sizeof(n->tag), tag);
    else               mt_etiquette(txt, n->tag, sizeof(n->tag));
    n->cree = now;
    n->due = now;                /* à revoir tout de suite            */
    n->ease = 2.5f;
    n->intervalle = 0;
    n->hash = h;
    return s->n++;
}

bool mt_supprime(mt_store_t *s, uint16_t idx)
{
    if (!s || idx >= s->n) return false;
    for (uint16_t i = idx; s->n && i + 1 < s->n; i++) s->notes[i] = s->notes[i + 1];
    s->n--;
    return true;
}

int mt_trouve_hash(const mt_store_t *s, uint32_t hash)
{
    for (uint16_t i = 0; i < s->n; i++)
        if (s->notes[i].hash == hash) return i;
    return -1;
}

/* ================================================================== */
/* persistance                                                        */
/* ================================================================== */
#define SEP '\x1f'

size_t mt_serialise(const mt_store_t *s, char *out, size_t cap)
{
    size_t o = 0;
    if (!s || !out) return 0;
    o += (size_t)snprintf(out + o, cap - o, "MT1\n");
    for (uint16_t i = 0; i < s->n && o + 200 < cap; i++) {
        const mt_note_t *n = &s->notes[i];
        char txt[MT_TXT_L], tag[MT_TAG_L];
        mt_copie(txt, sizeof(txt), n->txt);
        mt_copie(tag, sizeof(tag), n->tag);
        for (char *p = txt; *p; p++) if (*p == SEP || *p == '\n') *p = ' ';
        for (char *p = tag; *p; p++) if (*p == SEP || *p == '\n') *p = ' ';
        o += (size_t)snprintf(out + o, cap - o,
                "%u%c%lld%c%.2f%c%u%c%u%c%u%c%s%c%s\n",
                (unsigned)n->hash, SEP, (long long)n->cree, SEP, (double)n->ease, SEP,
                (unsigned)n->intervalle, SEP, (unsigned)n->reps, SEP, (unsigned)n->oublis,
                SEP, tag, SEP, txt);
    }
    return o;
}

bool mt_deserialise(mt_store_t *s, const char *in)
{
    if (!s || !in) return false;
    if (strncmp(in, "MT1\n", 4) != 0) return false;
    s->n = 0;
    const char *l = strchr(in, '\n');
    if (!l) return false;
    l++;
    while (*l && s->n < MT_NOTES_MAX) {
        char ligne[MT_TXT_L + 80];
        size_t k = 0;
        while (l[k] && l[k] != '\n' && k + 1 < sizeof(ligne)) { ligne[k] = l[k]; k++; }
        ligne[k] = 0;
        while (*l && *l != '\n') l++;
        if (*l == '\n') l++;

        char *ch[8] = { 0 };
        int nch = 0;
        ch[nch++] = ligne;
        for (char *p = ligne; *p && nch < 8; p++)
            if (*p == SEP) { *p = 0; ch[nch++] = p + 1; }
        if (nch < 8) continue;
        mt_note_t *n = &s->notes[s->n];
        memset(n, 0, sizeof(*n));
        n->hash = (uint32_t)strtoul(ch[0], NULL, 10);
        n->cree = (int64_t)strtoll(ch[1], NULL, 10);
        n->ease = (float)atof(ch[2]);
        n->intervalle = (uint16_t)atoi(ch[3]);
        n->reps = (uint8_t)atoi(ch[4]);
        n->oublis = (uint8_t)atoi(ch[5]);
        mt_copie(n->tag, sizeof(n->tag), ch[6]);
        mt_copie(n->txt, sizeof(n->txt), ch[7]);
        n->due = n->cree;      /* recalculée par la révision */
        s->n++;
    }
    return s->n > 0;
}

/* ================================================================== */
/* TF-IDF                                                             */
/* ================================================================== */
static int df_trouve(char vocab[][MT_TOKEN_MAX + 1], int nv, const char *mot)
{
    for (int i = 0; i < nv; i++) if (strcmp(vocab[i], mot) == 0) return i;
    return -1;
}

/* cosinus entre la question et chaque note, sans matrice : le vocabulaire et
   les documents sont parcourus une fois, ce qui tient dans 8 Mo de PSRAM. */
int mt_recherche(const mt_store_t *s, const char *question,
                 uint16_t res[], float scores[], int max)
{
    static char vocab[MT_TERMES_MAX][MT_TOKEN_MAX + 1];
    static uint16_t df[MT_TERMES_MAX];
    int nv = 0;
    if (!s || !question || s->n == 0 || max <= 0) return 0;

    /* 1. vocabulaire + fréquence documentaire */
    char mots[MT_TOKEN_MAX][MT_TOKEN_MAX + 1];
    for (uint16_t d = 0; d < s->n; d++) {
        int nt = mt_tokens(s->notes[d].txt, mots, MT_TOKEN_MAX);
        for (int i = 0; i < nt; i++) {
            int v = df_trouve(vocab, nv, mots[i]);
            if (v < 0) {
                if (nv >= MT_TERMES_MAX) continue;
                mt_copie(vocab[nv], MT_TOKEN_MAX + 1, mots[i]);
                df[nv] = 1;
                nv++;
            } else if (df[v] == 1) {
                df[v] = 2;      /* au moins deux documents : rare mais partagé */
            }
        }
    }

    /* 2. termes de la question */
    static char q[MT_TOKEN_MAX][MT_TOKEN_MAX + 1];
    int nq = mt_tokens(question, q, MT_TOKEN_MAX);
    if (nq == 0) return 0;

    /* 3. score de chaque note */
    static float sc[MT_NOTES_MAX];
    for (uint16_t d = 0; d < s->n; d++) sc[d] = 0.0f;

    for (int k = 0; k < nq; k++) {
        int v = df_trouve(vocab, nv, q[k]);
        if (v < 0) continue;
        float idf = 1.0f + logf((float)s->n / (float)df[v]);
        for (uint16_t d = 0; d < s->n; d++) {
            int nt = mt_tokens(s->notes[d].txt, mots, MT_TOKEN_MAX);
            int tf = 0;
            for (int i = 0; i < nt; i++) if (strcmp(mots[i], q[k]) == 0) tf++;
            if (tf) sc[d] += (1.0f + logf((float)tf)) * idf;
        }
    }

    /* 4. les meilleures, triées à la main (max 8, tri par insertion) */
    int n = 0;
    for (uint16_t d = 0; d < s->n; d++) {
        if (sc[d] <= 0.0f) continue;
        int pos = n;
        while (pos > 0 && sc[(int)res[pos - 1]] < sc[d]) {
            if (pos < max) { res[pos] = res[pos - 1]; scores[pos] = scores[pos - 1]; }
            pos--;
        }
        if (pos < max) { res[pos] = d; scores[pos] = sc[d]; }
        if (n < max) n++;
    }
    return n;
}

/* ================================================================== */
/* SM-2                                                               */
/* ================================================================== */
int mt_jours(int64_t a, int64_t b)
{
    int64_t d = (a > b ? a - b : b - a);
    return (int)(d / 86400);
}

void mt_sm2(mt_note_t *n, int qualite, int64_t now)
{
    if (!n) return;
    if (qualite < 3) {
        n->reps = 0;
        n->oublis++;
        n->intervalle = 1;                 /* revient dans l'heure        */
        n->ease = n->ease - 0.2f;
        if (n->ease < 1.3f) n->ease = 1.3f;
    } else {
        n->reps++;
        if (n->reps == 1)      n->intervalle = 1;
        else if (n->reps == 2) n->intervalle = 6;
        else {
            float iv = (float)n->intervalle * n->ease;
            if (iv < 1.0f) iv = 1.0f;
            if (iv > 365.0f) iv = 365.0f;
            n->intervalle = (uint16_t)(iv + 0.5f);
        }
        n->ease = n->ease + (0.1f - (float)(5 - qualite) * (0.08f + (float)(5 - qualite) * 0.02f));
        if (n->ease < 1.3f) n->ease = 1.3f;
        if (n->ease > 2.8f) n->ease = 2.8f;
    }
    if (qualite < 3) n->due = now + 3600;                 /* tout de suite  */
    else             n->due = now + (int64_t)n->intervalle * 86400;
}

int mt_dues(const mt_store_t *s, int64_t now, uint16_t res[], int max)
{
    int n = 0;
    for (uint16_t i = 0; i < s->n && n < max; i++)
        if (s->notes[i].due <= now) res[n++] = i;
    return n;
}

int mt_prochaine_due(const mt_store_t *s, int64_t now)
{
    int64_t meilleur = 0;
    for (uint16_t i = 0; i < s->n; i++) {
        if (s->notes[i].due <= now) return 0;
        if (meilleur == 0 || s->notes[i].due < meilleur) meilleur = s->notes[i].due;
    }
    if (!meilleur) return -1;
    return mt_jours(meilleur, now) + 1;
}

/* ================================================================== */
/* ce qui est montré                                                  */
/* ================================================================== */
int mt_resume(const mt_store_t *s, int64_t now, int *dues, int *aujourdhui)
{
    int d = 0, a = 0;
    for (uint16_t i = 0; i < s->n; i++) {
        if (s->notes[i].due <= now) d++;
        if (mt_jours(s->notes[i].cree, now) == 0) a++;
    }
    if (dues) *dues = d;
    if (aujourdhui) *aujourdhui = a;
    return s->n;
}

/* les 3 notes les plus récentes + celles à revoir */
void mt_brief(const mt_store_t *s, int64_t now, uint16_t res[], int max)
{
    int n = 0;
    for (int i = (int)s->n - 1; i >= 0 && n < max; i--) {
        if (s->notes[i].due <= now) res[n++] = (uint16_t)i;
    }
    for (int i = (int)s->n - 1; i >= 0 && n < max; i--) {
        bool deja = false;
        for (int k = 0; k < n; k++) if (res[k] == (uint16_t)i) deja = true;
        if (!deja) res[n++] = (uint16_t)i;
    }
}

/* ================================================================== */
/* dialogue avec le modèle                                            */
/* ================================================================== */
int mt_prompt(const mt_store_t *s, const char *question, char *out, size_t cap)
{
    if (!out || !cap) return 0;
    uint16_t res[4];
    float sc[4];
    int n = mt_recherche(s, question, res, sc, 4);

    size_t o = 0;
    o += (size_t)snprintf(out + o, cap - o,
        "Tu es MiniTelligence, le carnet d'une carte e-paper 200x200. "
        "Reponds en francais, en 40 mots maximum, sans markdown. "
        "Appuie-toi uniquement sur les notes fournies ; si elles ne suffisent pas, dis-le.\n\nNOTES:\n");
    for (int i = 0; i < n && o + 200 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "- [%s] %s\n",
                              s->notes[res[i]].tag, s->notes[res[i]].txt);
    if (n == 0) o += (size_t)snprintf(out + o, cap - o, "(aucune note)\n");
    o += (size_t)snprintf(out + o, cap - o, "\nQUESTION: %s\nREPONSE:", question ? question : "");
    return (int)o;
}

bool mt_extrait_reponse(const char *json, char *out, size_t cap)
{
    if (!json || !out || !cap) return false;
    const char *p = strstr(json, "\"content\":\"");
    if (!p) return false;
    p += 11;
    size_t o = 0;
    while (*p && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = ' '; break;
            case '"': out[o++] = '"'; break;
            case '\\': out[o++] = '\\'; break;
            case 'u': p += 4; break;                 /* \uXXXX : ignoré   */
            default: out[o++] = *p; break;
            }
            p++;
            continue;
        }
        if (*p == '"') break;
        out[o++] = *p++;
    }
    out[o] = 0;
    return o > 0;
}
