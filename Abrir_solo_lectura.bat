@echo off
rem Abre ServiGestWin10 sin pedir elevacion (modo solo lectura si no eres administrador)
start "" "%~dp0ServiGestWin10.exe" /noelevate
