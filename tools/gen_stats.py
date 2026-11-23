#!/usr/bin/env python3
"""Relit la sortie de tests/run_unit.sh et ecrit main/mt_tests_stats.h."""
import re, subprocess, sys, pathlib

racine = pathlib.Path(__file__).resolve().parent.parent
sortie = subprocess.run(["bash", str(racine / "tests/run_unit.sh")],
                        capture_output=True, text=True).stdout
total = 0
echecs = 0
for m in re.finditer(r"(\d+) verifications, (\d+) echec", sortie):
    total += int(m.group(1))
    echecs += int(m.group(2))
unite = 0
m = re.search(r"modele\s+(\d+) verifications", sortie)
if m: unite = int(m.group(1))

version = (racine / "VERSION").read_text().strip() if (racine / "VERSION").exists() else "1.0.0"
h = racine / "main" / "mt_tests_stats.h"
h.write_text(f"""/* genere par tools/gen_stats.py — ne pas editer a la main */
#ifndef MT_TESTS_STATS_H
#define MT_TESTS_STATS_H
#define MT_VERSION    "{version}"
#define MT_TESTS_UNIT  {unite}
#define MT_TESTS_INTEG {max(total - unite, 0)}
#define MT_TESTS_FAIL  {echecs}
#endif
""")
print(f"stats : {total} verifications, {echecs} echec(s), version {version}")
