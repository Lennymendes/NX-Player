#!/usr/bin/env python3
# Lists the French texts ("…"_t) that have no English entry in Resources/i18n/en.json.
#   python3 tools/i18n_check.py            (run from the repository root)
import json, subprocess, sys, tempfile, os
here = os.path.dirname(os.path.abspath(__file__))
with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f: out = f.name
subprocess.run([sys.executable, os.path.join(here, 'i18n_extract.py'), out], check=True, stdout=subprocess.DEVNULL)
keys = json.load(open(out, encoding='utf-8'))
en = json.load(open(os.path.join(here, '..', 'Resources', 'i18n', 'en.json'), encoding='utf-8'))
skip = lambda k: not any(c.isalpha() for c in k)
missing = [k for k in keys if k not in en and not skip(k)]
for k in missing: print(json.dumps(k, ensure_ascii=False))
print(f"{len(keys)} textes, {len(missing)} sans traduction", file=sys.stderr)
