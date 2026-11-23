#!/bin/bash
# Recette complète : tests, statistiques, firmware.
set -u
cd "$(dirname "$0")/.." || exit 2

echo "== 1/3 tests unitaires =="
./tests/run_unit.sh || exit 1

echo "== 2/3 statistiques pour l'ecran SYSTEME =="
python3 tools/gen_stats.py || exit 1

echo "== 3/3 firmware =="
if [ "${1:-}" != "--sans-build" ]; then
    env -i HOME="$HOME" IDF_TOOLS_PATH="$HOME/esp/tools" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        bash -c "source $HOME/esp/esp-idf/export.sh >/dev/null 2>&1; cd $(pwd) && idf.py build" \
        | grep -E "binary size|error:" | tail -3
fi
ls -l build/miniteligence.bin | awk '{print "firmware : " $5 " octets"}'
