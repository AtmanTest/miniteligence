/*  mt_model.h — le cerveau de MiniTelligence, sans une ligne d'ESP-IDF.
 *
 *  Tout ce qui pense vit ici, et se compile sur le PC : c'est ce qui permet
 *  de tester l'intelligence du carnet (recherche, révision, prompt) sans
 *  carte, sans réseau et sans LLM.
 *
 *  Trois idées, trois mécanismes :
 *
 *   1. MÉMOIRE  : chaque note est un fait daté, rangé sur la carte SD.
 *   2. RECHERCHE: un vrai TF-IDF (fréquence du terme x rareté dans le carnet)
 *                 classe les notes par rapport à une question. Zéro réseau,
 *                 zéro bibliothèque : c'est le classement qui décide quelles
 *                 notes partent dans le prompt du modèle.
 *   3. RÉVISION : SM-2 (l'algorithme d'Anki), calculé localement. Chaque note
 *                 devient une carte qui revient au bon moment ; une carte
 *                 sue s'éloigne, une carte ratée revient tout de suite.
 */
#ifndef MT_MODEL_H
#define MT_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MT_NOTES_MAX     96
#define MT_TXT_L        160
#define MT_TAG_L         16
#define MT_TERMES_MAX   512     /* vocabulaire suivi par le TF-IDF        */
#define MT_TOKEN_MAX     28     /* longueur maximale d'un mot              */
#define MT_QUEST_L      120

typedef struct {
    char     txt[MT_TXT_L];
    char     tag[MT_TAG_L];
    int64_t  cree;          /* epoch secondes                              */
    int64_t  due;           /* prochaine révision                          */
    float    ease;          /* SM-2 : 2.5 au départ, 1.3 au plancher       */
    uint16_t intervalle;    /* jours                                       */
    uint8_t  reps;          /* révisions réussies d'affilée                */
    uint8_t  oublis;        /* nombre de fois ratée                        */
    uint32_t hash;          /* identité stable (FNV-1a du texte)           */
} mt_note_t;

typedef struct {
    mt_note_t notes[MT_NOTES_MAX];
    uint16_t  n;
    uint32_t  graine;
} mt_store_t;

/* --- cycle de vie ------------------------------------------------- */
void mt_store_init(mt_store_t *s, uint32_t graine);
int  mt_ajoute(mt_store_t *s, const char *txt, const char *tag, int64_t now);
bool mt_supprime(mt_store_t *s, uint16_t idx);
int  mt_trouve_hash(const mt_store_t *s, uint32_t hash);
uint32_t mt_hash(const char *txt);

/* --- persistance : une ligne par note, champs séparés par 0x1F ------ */
size_t mt_serialise(const mt_store_t *s, char *out, size_t cap);
bool   mt_deserialise(mt_store_t *s, const char *in);

/* --- recherche : TF-IDF, classement par similarité cosinus --------- */
int  mt_tokens(const char *txt, char out[][MT_TOKEN_MAX + 1], int max);
void mt_radical(char *mot);                    /* pluriels, accents ASCII  */
int  mt_recherche(const mt_store_t *s, const char *question,
                  uint16_t res[], float scores[], int max);

/* --- révision : SM-2 ---------------------------------------------- */
void mt_sm2(mt_note_t *n, int qualite, int64_t now);   /* 0 raté .. 5 su */
int  mt_dues(const mt_store_t *s, int64_t now, uint16_t res[], int max);
int  mt_prochaine_due(const mt_store_t *s, int64_t now);

/* --- ce qui est montré -------------------------------------------- */
int  mt_resume(const mt_store_t *s, int64_t now, int *dues, int *aujourdhui);
void mt_brief(const mt_store_t *s, int64_t now, uint16_t res[], int max);

/* --- dialogue avec le modèle --------------------------------------- */
/* construit le prompt : consignes + notes retrouvées + question */
int  mt_prompt(const mt_store_t *s, const char *question, char *out, size_t cap);
/* extrait le texte d'une réponse JSON OpenAI/Groq ("content":"...") */
bool mt_extrait_reponse(const char *json, char *out, size_t cap);
/* étiquette courte tirée des mots de la note (1er mot significatif) */
void mt_etiquette(const char *txt, char *out, size_t cap);

/* --- texte --------------------------------------------------------- */
void mt_ascii(const char *in, char *out, size_t cap);
void mt_copie(char *dst, size_t cap, const char *src);
int  mt_jours(int64_t a, int64_t b);          /* jours entiers entre deux dates */

#endif /* MT_MODEL_H */
