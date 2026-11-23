#!/bin/bash
# flash.sh — construit, flashe et VERIFIE la carte, sans chercher le port a la main.
#
#   tools/flash.sh              build + flash + relecture du log de demarrage
#   tools/flash.sh --sans-build flashe le binaire deja construit
#
# Sort en 2 si aucune carte n'est branchee : ce n'est pas une erreur de build,
# c'est un cable absent. Le port est detecte (ttyACM pour l'USB natif du S3,
# ttyUSB pour un adaptateur CP210x/CH340).
set -u
cd "$(dirname "$0")/.." || exit 2
version=$(cat VERSION)

port=""
for p in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
    [ -e "$p" ] && port="$p" && break
done
if [ -z "$port" ]; then
    echo "FLASH : aucune carte branchee (aucun /dev/ttyACM* ni /dev/ttyUSB*)"
    echo "        brancher la carte en USB, puis relancer tools/flash.sh"
    exit 2
fi
echo "FLASH : carte sur $port, version $version"

if [ "${1:-}" != "--sans-build" ]; then
    echo "FLASH : compilation"
    if ! env -i HOME="$HOME" PATH=/usr/local/bin:/usr/bin:/bin IDF_TOOLS_PATH="$HOME/esp/tools" \
        bash -c '. '"$HOME"'/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py build' > build/flash-build.log 2>&1; then
        echo "FLASH : compilation ECHEC (voir build/flash-build.log)"
        exit 1
    fi
    echo "FLASH : firmware $(stat -c %s build/miniteligence.bin) octets"
fi

echo "FLASH : ecriture"
if ! env -i HOME="$HOME" PATH=/usr/local/bin:/usr/bin:/bin IDF_TOOLS_PATH="$HOME/esp/tools" \
    bash -c '. '"$HOME"'/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py -p '"$port"' flash' > build/flash.log 2>&1; then
    echo "FLASH : ECHEC (voir build/flash.log)"
    tail -15 build/flash.log
    exit 1
fi
grep -E "Hash of data verified|Leaving...|Hard resetting" build/flash.log | tail -3

# verification : on relit les 15 premieres secondes du log serie applicatif
echo "FLASH : relecture du log (15 s)"
log=$(timeout 15 cat "$port" 2>/dev/null | tr -d '\r')
ligne=$(printf '%s\n' "$log" | grep -a "FDT " | tail -1)
if [ -n "$ligne" ]; then
    echo "FLASH : $ligne"
    case "$ligne" in
        *"$version"*) echo "FLASH : version confirmee ($version)";;
        *) echo "FLASH : ATTENTION, la carte annonce autre chose que $version";;
    esac
    exit 0
fi
echo "FLASH : ecrit, mais aucun log applicatif lu."
echo "        appuyer sur RESET (ou rebrancher) et relancer : timeout 15 cat $port"
exit 1
