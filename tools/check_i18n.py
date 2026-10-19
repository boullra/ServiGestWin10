#!/usr/bin/env python3
"""Comprueba la coherencia de la localización de ServiGestWin10.

1. Todas las claves usadas en src/*.cpp existen en lang/en.lang (o como .one/.other).
2. Todos los idiomas tienen exactamente las mismas claves que en.lang.
3. Cada traducción usa los mismos marcadores {N} que el original inglés.
4. No hay claves sin usar en en.lang (aviso, salvo crit.* y mode/state que se usan indirectamente).
Sale con código 1 si hay errores.
"""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
LANGS = ["en", "es", "fr", "zh-CN"]

def load(path):
    entries, errors = {}, []
    for n, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            errors.append(f"{path.name}:{n}: falta '='")
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip()
        if k.startswith("@"):
            continue
        if k in entries:
            errors.append(f"{path.name}:{n}: clave duplicada {k}")
        entries[k] = v
    return entries, errors

def placeholders(s):
    return sorted(set(re.findall(r"(?<!\{)\{(\d+)\}(?!\})", s)))

errors, warnings = [], []
cats = {}
for l in LANGS:
    cats[l], e = load(root / "lang" / f"{l}.lang")
    errors += e
en = cats["en"]

key_re = re.compile(r'"([a-z][a-z0-9_]*(?:\.[A-Za-z0-9_-]+)+)"')
ignore_ext = (".hpp", ".h", ".lang", ".csv", ".exe", ".dll", ".ico", ".rc")
used = set()
for src in sorted((root / "src").glob("*.cpp")):
    for m in key_re.finditer(src.read_text(encoding="utf-8")):
        k = m.group(1)
        if k.endswith(ignore_ext) or k.startswith(("microsoft.", "nt.", "diagnosticshub.")):
            continue
        used.add(k)
        if k not in en and f"{k}.one" not in en:
            errors.append(f"{src.name}: clave '{k}' no existe en en.lang")

for l in LANGS[1:]:
    c = cats[l]
    for k in sorted(set(en) - set(c)):
        errors.append(f"{l}.lang: falta la clave {k}")
    for k in sorted(set(c) - set(en)):
        errors.append(f"{l}.lang: clave {k} no existe en en.lang")
    for k in sorted(set(en) & set(c)):
        if placeholders(en[k]) != placeholders(c[k]):
            errors.append(f"{l}.lang: {k}: marcadores {placeholders(c[k])} != en {placeholders(en[k])}")

for k in sorted(en):
    base = k.rsplit(".", 1)[0] if k.endswith((".one", ".other")) else k
    if base not in used and k not in used and not k.startswith(("crit.", "mode.", "state.")):
        warnings.append(f"en.lang: clave sin usar {k}")

for w in warnings:
    print("AVISO:", w)
for e in errors:
    print("ERROR:", e)
print(f"i18n: {len(en)} claves, {len(used)} usadas en el código, {len(errors)} errores, {len(warnings)} avisos")
sys.exit(1 if errors else 0)
