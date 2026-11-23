#!/bin/bash
# sauvegarde_sd.sh — copie la derniere livraison sur la carte SD, SANS RIEN DEMANDER.
#
#   tools/sauvegarde_sd.sh
#
# Cherche une carte SD montee (carte du PC ou lecteur USB), y copie :
#   - l'archive source de la version courante (zip de livraison)
#   - la derniere sauvegarde complete (tar.gz + bundle git)
#   - un INFOS.txt avec version, commit, tag
# Si aucune carte n'est montee, le dit et sort en 2 : ce n'est pas une erreur
# de build, c'est une carte absente (a rebrancher et relancer).
set -u
cd "$(dirname "$0")/.." || exit 2
version=$(cat VERSION)
commit=$(git rev-parse --short HEAD 2>/dev/null || echo "hors-git")
tag=$(git describe --tags --exact-match 2>/dev/null || git describe --tags --abbrev=0 2>/dev/null || echo "-")
zip="$HOME/livraisons/fdj/batmino-fdj-$version-src.zip"
dernier=$(ls -dt "$HOME"/Sauvegardes/batmino/*/ 2>/dev/null | head -1)

# ou est la carte ?
carte=""
for c in /run/media/"$USER"/* /media/"$USER"/* /media/* /mnt/sdcard; do
    [ -d "$c" ] || continue
    case "$c" in *batmino*) continue ;; esac
    carte="$c"; break
done
if [ -z "$carte" ]; then
    # pas de carte sur le PC : on passe par la carte BATMino (micro SD interne)
    ip="${BATMINO_IP:-192.168.8.209}"
    if curl -s --max-time 3 "http://$ip/etat" >/dev/null 2>&1; then
        echo "SD : pas de carte montee sur le PC, copie via la carte $ip"
        ver="$(cat VERSION 2>/dev/null | tr -d '\n')"
        [ -f "$zip" ] && curl -s --max-time 120 --data-binary "@$zip" \
            "http://$ip/fichier?p=/sdcard/FDT-${ver}-src.zip" | sed 's/^/SD : zip /'
        if [ -n "$dernier" ]; then
            j="$(basename "$dernier")"
            for f in "$dernier"/*.tar.gz; do
                [ -f "$f" ] || continue
                curl -s --max-time 120 --data-binary "@$f" \
                    "http://$ip/fichier?p=/sdcard/FDT-${j}-batmino.tar.gz" | sed 's/^/SD : tar /'
            done
            for f in "$dernier"/*.bundle; do
                [ -f "$f" ] || continue
                curl -s --max-time 120 --data-binary "@$f" \
                    "http://$ip/fichier?p=/sdcard/FDT-${j}-batmino.bundle" | sed 's/^/SD : bundle /'
            done
        fi
        echo "SD : fichiers envoyes sur la micro SD de la carte"
        exit 0
    fi
    echo "SD : aucune carte montee et carte BATMino injoignable (ni SD, ni Wi-Fi)"
    exit 2
fi

dest="$carte/BATMINO"
mkdir -p "$dest"
if [ -f "$zip" ]; then
    cp -f "$zip" "$dest/" && echo "SD : $(basename "$zip") copie"
else
    echo "SD : archive $zip absente (lancer tools/recette.sh d'abord)"
fi
if [ -n "$dernier" ]; then
    cp -f "$dernier"/*.tar.gz "$dernier"/*.bundle "$dernier"/SHA256SUMS "$dest/" 2>/dev/null
    echo "SD : sauvegarde $(basename "$dernier") copiee"
fi
{
    echo "BATMino x FDJ"
    echo "version : $version"
    echo "commit  : $commit"
    echo "tag     : $tag"
    echo "date    : $(date '+%Y-%m-%d %H:%M')"
    echo "copie   : $dest"
} > "$dest/INFOS.txt"
sync
ls -lh "$dest" | tail -6
