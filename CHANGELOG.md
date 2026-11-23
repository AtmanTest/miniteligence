# Journal des versions

## 1.0.0 — 2026-11-23 — MiniTelligence

Première version complète, sur la carte BATMino.

- **Mémoire persistante** : carnet de 96 notes sur la micro SD (`/sdcard/mt.db`,
  nom 8.3 car la carte est montée sans noms longs), sauvegardé à chaque ajout.
- **Recherche locale** : TF-IDF avec mots vides, radicaux (pluriels, « aux »),
  accents ramenés en ASCII, classement par somme tf·idf. Aucune dépendance.
- **Révision SM-2** : intervalles 1 → 6 → ×ease, plancher d'ease 1,3, carte
  ratée qui revient dans l'heure, échéance calculée sur la carte.
- **Dialogue** : prompt borné aux 4 notes retrouvées, appel Groq ou DeepSeek,
  extraction du texte JSON, repli hors ligne sur la recherche seule, et « je ne
  sais pas » quand rien ne correspond.
- **Capteurs** : SHTC3 (température, humidité) et RTC en I2C, mesures réelles,
  « N.D. » si le capteur ne répond pas.
- **6 écrans** e-paper en 5×10 : briefing, mémoire, question, révision,
  capteurs, système.
- **API de recette** : /etat /ecran /bouton /note /ask /supprime /liste
  /efface /carnet.
- 67 vérifications sur le modèle (PC, sans carte), 0 échec.
