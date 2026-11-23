/*  test_model.cpp — l'intelligence du carnet, testée sans carte et sans réseau.
 *
 *  Ce qui est vérifié ici n'est pas du code : c'est le comportement.
 *    - un mot écrit avec accent ou au pluriel retrouve la même note ;
 *    - une question classe la bonne note en premier, et pas une autre ;
 *    - SM-2 fait revenir une carte ratée tout de suite et éloigne une carte sue ;
 *    - le prompt envoyé au modèle contient les notes retrouvées, pas tout le carnet ;
 *    - une sauvegarde relue rend exactement le même carnet.
 */
#include "check.h"
#include "mt_model.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const int64_t T0 = 1760000000LL;      /* 2025-10-09, une date fixe */

static void test_texte(void)
{
    char a[128];
    mt_ascii("Le café à Noël, très chaud", a, sizeof(a));
    OK(strstr(a, "cafe") != NULL, "é devient e");
    OK(strstr(a, "noel") != NULL, "ë devient e");
    mt_ascii("Ça coûte cher", a, sizeof(a));
    OK(strstr(a, "ca coute") != NULL, "ç et û tombent juste");

    char mots[16][MT_TOKEN_MAX + 1];
    int n = mt_tokens("Les voitures des journaux", mots, 16);
    OK(n == 2, "les mots vides sont ecartes");
    OK(strcmp(mots[0], "voiture") == 0, "pluriel ramené au singulier");
    OK(strcmp(mots[1], "journal") == 0, "aux -> al");

    n = mt_tokens("a y et de la", mots, 16);
    OK(n == 0, "les mots d'un seul caractère sont ignorés");
}

static void test_carnet(void)
{
    mt_store_t s;
    mt_store_init(&s, 1);
    OK(s.n == 0, "carnet vide au départ");

    int i = mt_ajoute(&s, "La voiture de Thasin est une Renault", NULL, T0);
    OK(i == 0, "premiere note acceptee");
    OK(strcmp(s.notes[0].tag, "VOITURE") == 0, "etiquette tiree du texte");
    OK(s.notes[0].ease == 2.5f, "ease SM-2 par defaut");
    OK(s.notes[0].due == T0, "une note neuve est due tout de suite");

    OK(mt_ajoute(&s, "La voiture de Thasin est une Renault", NULL, T0) == -3,
       "la meme note n'est pas ajoutee deux fois");
    OK(mt_ajoute(&s, "", NULL, T0) == -1, "une note vide est refusee");

    mt_ajoute(&s, "Le chat dort sur le canape", "CHAT", T0);
    mt_ajoute(&s, "Recette de la tarte aux pommes", NULL, T0);
    OK(s.n == 3, "trois notes");

    OK(mt_trouve_hash(&s, mt_hash("Le chat dort sur le canape")) == 1, "hash stable");
    OK(mt_supprime(&s, 1), "suppression acceptee");
    OK(s.n == 2 && strstr(s.notes[1].txt, "tarte") != NULL, "le carnet se tasse");
}

static void test_recherche(void)
{
    mt_store_t s;
    mt_store_init(&s, 2);
    mt_ajoute(&s, "La voiture de Thasin est une Renault", NULL, T0);
    mt_ajoute(&s, "Le chat dort sur le canape", NULL, T0);
    mt_ajoute(&s, "Recette de la tarte aux pommes", NULL, T0);
    mt_ajoute(&s, "Le train pour Paris part a huit heures", NULL, T0);

    uint16_t res[4];
    float sc[4];
    int n = mt_recherche(&s, "quand part le train", res, sc, 4);
    OK(n >= 1, "une question simple trouve au moins une note");
    OK(strstr(s.notes[res[0]].txt, "train") != NULL, "le train sort en premier");
    OK(sc[0] > 0.0f, "le score est positif");

    n = mt_recherche(&s, "chat canape", res, sc, 4);
    OK(n >= 1 && strstr(s.notes[res[0]].txt, "chat") != NULL, "le chat sort sur 'chat'");

    n = mt_recherche(&s, "voiture", res, sc, 4);
    OK(n == 1, "un seul document parle de voiture");
    OK(res[0] == 0, "et c'est le bon");

    n = mt_recherche(&s, "sous-marin nucleaire", res, sc, 4);
    OK(n == 0, "aucune note ne parle de sous-marin : rien n'est inventé");

    /* la rareté compte : "tarte" est plus discriminant que "le" */
    n = mt_recherche(&s, "le tarte", res, sc, 4);
    OK(n >= 1 && strstr(s.notes[res[0]].txt, "tarte") != NULL, "le mot rare gagne");
}

static void test_sm2(void)
{
    mt_note_t n;
    memset(&n, 0, sizeof(n));
    n.ease = 2.5f;
    n.due = T0;

    mt_sm2(&n, 5, T0);
    OK(n.intervalle == 1, "premiere reussite : revient demain");
    OK(n.due == T0 + 86400, "echeance a 1 jour");
    OK(n.ease > 2.5f, "ease monte apres une bonne reponse");

    mt_sm2(&n, 5, T0 + 86400);
    OK(n.intervalle == 6, "deuxieme reussite : six jours");

    mt_sm2(&n, 5, T0 + 7 * 86400);
    OK(n.intervalle > 6, "troisieme reussite : l'intervalle s'allonge");
    OK(n.intervalle <= 365, "et reste borne a un an");

    mt_sm2(&n, 2, T0 + 30 * 86400);
    OK(n.intervalle == 1, "une carte ratee revient tout de suite");
    OK(n.reps == 0, "le compteur de reussites repart a zero");
    OK(n.oublis == 1, "l'oubli est compte");

    for (int i = 0; i < 20; i++) mt_sm2(&n, 0, T0);
    OK(n.ease >= 1.3f, "l'ease ne descend jamais sous 1.3");
}

static void test_echeances(void)
{
    mt_store_t s;
    mt_store_init(&s, 3);
    mt_ajoute(&s, "note une", NULL, T0);
    mt_ajoute(&s, "note deux", NULL, T0);
    mt_ajoute(&s, "note trois", NULL, T0);

    mt_sm2(&s.notes[0], 5, T0);                 /* revient demain        */
    mt_sm2(&s.notes[1], 5, T0);                 /* revient demain        */
    mt_sm2(&s.notes[1], 5, T0 + 86400);         /* puis dans six jours   */
    mt_sm2(&s.notes[2], 2, T0);                 /* ratee : dans l'heure  */

    OK(s.notes[2].due == T0 + 3600, "une carte ratee revient dans l'heure");

    uint16_t res[4];
    OK(mt_dues(&s, T0, res, 4) == 0, "juste apres la revision, plus rien n'est du");
    int n = mt_dues(&s, T0 + 2 * 3600, res, 4);
    OK(n == 1 && res[0] == 2, "deux heures plus tard, seule la ratee revient");
    n = mt_dues(&s, T0 + 86400, res, 4);
    OK(n == 2, "le lendemain, la ratee et la premiere note sont dues");
    OK(mt_prochaine_due(&s, T0 + 86400 + 60) == 0, "des cartes sont dues : la prochaine, c'est maintenant");

    mt_store_t u;
    mt_store_init(&u, 8);
    mt_ajoute(&u, "une seule note", NULL, T0);
    mt_sm2(&u.notes[0], 5, T0);
    mt_sm2(&u.notes[0], 5, T0 + 86400);
    OK(mt_prochaine_due(&u, T0 + 2 * 86400) == 6, "la note sue revient dans 5 jours (arrondi a 6)");

    int dues = 0, auj = 0;
    mt_resume(&s, T0 + 86400, &dues, &auj);
    OK(dues == 2, "resume : deux cartes dues");
    OK(auj == 0, "resume : aucune note creee aujourd'hui");
}

static void test_persistance(void)
{
    mt_store_t s, r;
    mt_store_init(&s, 4);
    mt_ajoute(&s, "Le café de la gare coûte deux euros", "CAFE", T0);
    mt_ajoute(&s, "Penser à appeler le dentiste | urgence", NULL, T0);
    mt_sm2(&s.notes[0], 5, T0);

    char buf[4096];
    size_t n = mt_serialise(&s, buf, sizeof(buf));
    OK(n > 20, "la sauvegarde ecrit quelque chose");
    OK(n < sizeof(buf), "et tient dans le tampon");

    OK(mt_deserialise(&r, buf), "la sauvegarde se relit");
    OK(r.n == s.n, "meme nombre de notes");
    OK(strcmp(r.notes[0].txt, s.notes[0].txt) == 0, "texte identique");
    OK(strcmp(r.notes[1].tag, s.notes[1].tag) == 0, "etiquette identique");
    OK(r.notes[0].hash == s.notes[0].hash, "identite conservee");
    OK(r.notes[0].ease == s.notes[0].ease, "ease conserve");

    OK(!mt_deserialise(&r, "n'importe quoi"), "un fichier inconnu est refuse");
}

static void test_prompt(void)
{
    mt_store_t s;
    mt_store_init(&s, 5);
    mt_ajoute(&s, "Le train pour Paris part a huit heures", NULL, T0);
    mt_ajoute(&s, "Le chat dort sur le canape", NULL, T0);

    char p[1024];
    int n = mt_prompt(&s, "a quelle heure part le train", p, sizeof(p));
    OK(n > 100, "le prompt est construit");
    OK(strstr(p, "NOTES:") != NULL, "il contient le bloc de notes");
    OK(strstr(p, "train") != NULL, "il contient la note pertinente");
    OK(strstr(p, "chat") == NULL, "il ne contient pas une note hors sujet");
    OK(strstr(p, "QUESTION: a quelle heure part le train") != NULL, "la question est dedans");
    OK(strstr(p, "40 mots") != NULL, "la consigne de longueur est dedans");

    mt_store_t vide;
    mt_store_init(&vide, 6);
    n = mt_prompt(&vide, "et sinon ?", p, sizeof(p));
    OK(strstr(p, "(aucune note)") != NULL, "un carnet vide est dit explicitement");
}

static void test_reponse(void)
{
    char out[256];
    const char *json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"A huit heures.\\nGare de Lyon.\"}}]}";
    OK(mt_extrait_reponse(json, out, sizeof(out)), "le texte est extrait");
    OK(strcmp(out, "A huit heures.\nGare de Lyon.") == 0, "les echappements sont rendus");

    OK(!mt_extrait_reponse("{\"error\":\"x\"}", out, sizeof(out)), "pas de content : rien");
    OK(mt_extrait_reponse("{\"content\":\"caf\\u00e9\"}", out, sizeof(out)), "un \\u est saute");
    OK(strstr(out, "caf") != NULL, "et le texte reste lisible");
}

static void test_brief(void)
{
    mt_store_t s;
    mt_store_init(&s, 7);
    mt_ajoute(&s, "note recente A", NULL, T0);
    mt_ajoute(&s, "note recente B", NULL, T0 + 10);
    mt_ajoute(&s, "note recente C", NULL, T0 + 20);

    uint16_t res[3];
    mt_brief(&s, T0 + 20, res, 3);
    OK(res[0] == 2, "le brief commence par la note la plus recente");
    OK(res[1] == 1 && res[2] == 0, "puis les precedentes, sans doublon");
}

int main(void)
{
    test_texte();
    test_carnet();
    test_recherche();
    test_sm2();
    test_echeances();
    test_persistance();
    test_prompt();
    test_reponse();
    test_brief();
    FIN("modele");
}
