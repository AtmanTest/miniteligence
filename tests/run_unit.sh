#!/bin/bash
# Recette de test de MiniTelligence — sur le PC, sans carte.
set -u
cd "$(dirname "$0")/.." || exit 2
mkdir -p build/tests
rc=0
filtre="${1:-}"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I main -I tests/unit"
LIBS="-lm"

suite() {
    local nom="$1" bin="$2"; shift 2
    if [ -n "$filtre" ] && [ "$filtre" != "$nom" ]; then return 0; fi
    echo "— $nom"
    if ! g++ $CXXFLAGS -o "build/tests/$bin" "$@" $LIBS; then
        echo "  COMPILATION ECHOUEE"; rc=$((rc+1)); return 0
    fi
    "./build/tests/$bin" || rc=$((rc+1))
}

suite modele test_model tests/unit/test_model.cpp main/mt_model.cpp

if [ $rc -eq 0 ]; then echo; echo "unitaires : tout bon"; else echo; echo "unitaires : $rc suite(s) en echec"; fi
exit $rc
