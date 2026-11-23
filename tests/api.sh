#!/bin/bash
# Tests d'integration et de composant, contre la carte reelle.
#   bash tests/api.sh [ip] [rapide|complet]
# Par defaut "rapide" : 5 secondes, sans le cerveau ni la voix (c'est long et
# on n'a pas besoin de le rejouer a chaque bricole d'ecran).
# "complet" ajoute /dit, /tts et la zone d'echange du disque.
IP="${1:-192.168.8.208}"
MODE="${2:-rapide}"
B="http://$IP"
ok=0; ko=0

verif() {  # verif <nom> <condition-shell>
    if eval "$2"; then ok=$((ok+1)); printf '  ok   %s\n' "$1"
    else ko=$((ko+1)); printf '  KO   %s\n' "$1"; fi
}

echo "tests d'integration ($MODE) : $B"

# --- composant : etat (toujours)
S=$(curl -s --max-time 8 "$B/state")
verif "/state repond" '[ -n "$S" ]'
verif "/state a heap_interne" 'echo "$S" | grep -q heap_interne'
verif "/state a historique_tours" 'echo "$S" | grep -q historique_tours'

# --- composant : carte SD (lecture seule, rapide)
L=$(curl -s --max-time 8 "$B/sd")
verif "/sd liste la racine" 'echo "$L" | grep -q "\"presente\":true"'
P=$(curl -s --max-time 15 "$B/part?op=info")
verif "/part voit 2 partitions" '[ "$(echo "$P" | grep -o debut_lba | wc -l)" -eq 2 ]'
verif "/part zone brute ~23 Go" 'echo "$P" | grep -q "\"go\":2[0-9]\."'

# --- ecran : le framebuffer se lit
E=$(curl -s --max-time 8 "$B/ecran")
verif "/ecran renvoie 100 lignes" '[ "$(echo "$E" | wc -l)" -ge 100 ]'

if [ "$MODE" = "complet" ]; then
    # --- composant : zone d'echange (long)
    ST=$(curl -s --max-time 120 "$B/part?op=st&n=1")
    verif "zone d'echange : ecriture+relecture verifiee" 'echo "$ST" | grep -q "\"verifie\":true"'

    # --- integration : cerveau + memoire + clone
    D1=$(curl -s --max-time 70 --get --data-urlencode "t=quel est mon nom et que suis-je en train de construire ?" "$B/dit")
    verif "/dit repond" 'echo "$D1" | grep -q "\"ok\":true"'
    verif "le clone est connu (Taz cite)" 'echo "$D1" | grep -qi "taz"'
    N1=$(echo "$D1" | sed -n 's/.*"dit":"\([^"]*\)".*/\1/p' | wc -c)
    verif "reponse developpee (> 120 caracteres)" "[ $N1 -gt 120 ]"

    D2=$(curl -s --max-time 70 --get --data-urlencode "t=et donc quelle est la prochaine etape ?" "$B/dit")
    verif "deuxieme tour accepte (historique)" 'echo "$D2" | grep -q "\"ok\":true"'
    H=$(curl -s --max-time 8 "$B/state" | sed -n 's/.*"historique_tours":\([0-9]*\).*/\1/p')
    verif "l'historique grandit ($H tours)" "[ ${H:-0} -ge 4 ]"

    # --- integration : voix (TTS) sans micro
    T=$(curl -s --max-time 60 --get --data-urlencode "q=test de la voix" "$B/tts")
    verif "/tts accepte" 'echo "$T" | grep -q "\"ok\":true"'
fi

printf 'resultat : %d ok, %d ko\n' "$ok" "$ko"
[ "$ko" -eq 0 ]
