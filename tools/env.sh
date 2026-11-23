#!/bin/bash
# Environnement ESP-IDF sans export.sh (le venv /opt/espressif-tools est casse :
# pydantic_core manquant). On sourit ce fichier puis on appelle idf.py.
export IDF_PATH=/opt/esp-idf
export IDF_TOOLS_PATH=/opt/espressif-tools
export IDF_PYTHON_ENV_PATH=/opt/espressif-tools/python_env/idf5.5_py3.14_env
export PYTHONPATH="$HOME/.idf-extra${PYTHONPATH:+:$PYTHONPATH}"
T=/opt/espressif-tools/tools
export PATH="$T/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin:$T/esp32ulp-elf/2.38_20240113/esp32ulp-elf/bin:$PATH"
idf() { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
