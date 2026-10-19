#!/usr/bin/env bash
# Compila ServiGestWin10.exe (Windows x64) con MinGW-w64 desde Linux, y los tests.
set -euo pipefail
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p build
# Coherencia de los archivos de idioma (claves, marcadores, cobertura del código)
python3 tools/check_i18n.py
$WINDRES -I src res/app.rc -O coff -o build/app.res.o
FLAGS="-std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -municode"
$CXX $FLAGS -mwindows -static -s src/main.cpp src/scm.cpp src/core.cpp src/knowledge.cpp src/i18n.cpp src/lang_win.cpp build/app.res.o \
    -lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -luxtheme -ladvapi32 -lkernel32 -lgdi32 -luser32 -o build/ServiGestWin10.exe
# tests de la lógica portable, como ejecutable de consola de Windows
$CXX -std=c++17 -O2 -Wall -Wextra -static -s -Isrc tests/test_core.cpp src/core.cpp src/knowledge.cpp src/i18n.cpp -o build/test_core.exe
echo "OK: build/ServiGestWin10.exe  (tests: build/test_core.exe lang)"
