# MiniTelligence 1.0 — le carnet qui pense

Une carte ESP32-S3 de 200×200 pixels, deux boutons, une dalle e-paper et un
micro SD. Pas de clavier, pas d'écran couleur, pas de GPU — et pourtant :

- elle **retient** ce qu'on lui écrit (mémoire persistante sur la carte) ;
- elle **cherche** dans cette mémoire sans réseau (TF-IDF, calculé sur la carte) ;
- elle **planifie** ses révisions (SM-2, comme Anki, calculé sur la carte) ;
- elle **mesure** son environnement (SHTC3 : température, humidité) ;
- elle **raisonne** avec un modèle de langue, mais seulement avec les notes
  pertinentes, et elle dit quand elle ne sait pas.

C'est une application intelligente au sens utile : l'intelligence locale
(recherche + planification) porte le produit, le LLM n'est qu'un amplificateur.

## Les 6 écrans

| # | Écran | Ce qu'il montre |
|---|-------|-----------------|
| 1 | **BRIEFING** | date/heure, nombre de notes, cartes dues, dernière note, température |
| 2 | **MÉMOIRE** | les notes, 4 par page, avec leur âge et leur « ease » SM-2 |
| 3 | **QUESTION** | la question posée par le web et la réponse du modèle (8 lignes) |
| 4 | **RÉVISION** | une carte à la fois : texte, puis « je savais » / « à revoir » |
| 5 | **CAPTEURS** | température, humidité, batterie, Wi-Fi, SD, RAM, latences |
| 6 | **SYSTÈME** | version, nombre de vérifications, API, effacement |

Tout est en police 5×10 (10 px de haut), noir sur blanc, rien en dessous.

## Ce qui est vraiment intelligent, et où c'est calculé

| Mécanisme | Où | Pourquoi local |
|-----------|----|----------------|
| TF-IDF + cosinus | `mt_model.cpp`, RAM | choisir 4 notes parmi 96, sans réseau, en quelques ms |
| SM-2 (Anki) | `mt_model.cpp` | l'échéance de chaque carte est calculée sur la carte |
| Étiquetage | `mt_model.cpp` | le tag d'une note sort de son premier mot utile |
| Prompt borné | `mt_model.cpp` | le modèle ne voit que les notes retrouvées, jamais tout le carnet |
| Repli hors ligne | `mt_app.cpp` | sans réseau, la réponse vient de la recherche seule |
| Capteurs | `mt_app.cpp` | SHTC3 + RTC en I2C, mesures réelles, « N.D. » si absent |

Une question sans note correspondante et sans réseau ne produit **jamais** une
réponse inventée : l'écran dit qu'il ne sait pas.

## API de recette (le carnet se pilote en HTTP)

| Route | Effet |
|-------|-------|
| `GET /etat` | écran, notes, dues, wifi, heure, SD, temp, latences, version |
| `GET /ecran` | framebuffer 200×200 en hexadécimal |
| `GET /bouton?n=0\|1&m=court\|double\|long` | injecter un geste |
| `GET /note?t=...` | ajouter une note (décodage URL complet) |
| `GET /ask?q=...` | poser une question : TF-IDF + LLM + réponse |
| `GET /supprime?i=3` | supprimer une note |
| `GET /liste` | contenu de la micro SD |
| `POST /fichier?p=/sdcard/NOM` | poser un fichier sur la carte (vide = effacer) |

```bash
curl "http://192.168.8.209/note?t=Le%20train%20pour%20Paris%20part%20a%20huit%20heures"
curl "http://192.168.8.209/ask?q=a%20quelle%20heure%20part%20le%20train"
```

## Gestes

| Geste | Effet |
|-------|-------|
| B2 court | écran suivant |
| B2 long | écran précédent |
| B2 double | action de l'écran (réviser, relancer, mesurer) |
| B1 court | action secondaire (dévoiler une carte, ouvrir) |
| B1 double | « à revoir » pendant la révision |
| B1 long | écran suivant (secours) |

## Tests

```bash
./tests/run_unit.sh     # modèle seul, sur PC, sans carte ni réseau
python3 tools/gen_stats.py
```

Les tests couvrent le comportement, pas l'implémentation : accents et pluriels,
classement TF-IDF (le mot rare gagne), SM-2 (intervalle 1 → 6 → ×ease, plancher
d'ease 1,3, carte ratée qui revient dans l'heure), sauvegarde/relecture à
l'identique, prompt qui ne contient que les notes pertinentes, extraction JSON
avec échappements.

## Matériel et construction

ESP32-S3-PICO-1 (8 Mo flash, 8 Mo PSRAM), dalle e-paper 200×200 1 bit, deux
boutons, micro SD, SHTC3 + RTC sur I2C, codec audio.

```bash
cp main/secrets.h.example main/secrets.h    # Wi-Fi + clés, jamais versionné
idf.py build && idf.py -p /dev/ttyACM0 flash
```

## Limites assumées

- **Pas de reconnaissance vocale sur la carte** : un modèle STT ne tient pas
  dans 8 Mo. La saisie passe par le web.
- **Pas de TTS** dans cette version : la réponse est écrite, pas lue.
- Le LLM dépend du réseau ; hors ligne, la recherche locale répond seule.
- 96 notes maximum : au-delà, l'écran le dit et refuse d'écraser.

## Auteur

Thasin Jahangir — @paristzou — « juste pour les filles ».
