#!/usr/bin/env python3
"""Adds the _t suffix (juce::String from UTF-8) to every string literal that contains non-ASCII
characters in the given C++ files. Leaves alone literals already suffixed, includes, test-section
names and lines marked // raw-utf8."""
import re, sys
lit = re.compile(r'(?<![A-Za-z0-9_])"((?:[^"\\\n]|\\.)*)"(?!_t)')
for path in sys.argv[1:]:
    if "/win/" in path or path.endswith("Text.h"):
        continue
    src = open(path, encoding='utf-8').read()
    out = []
    changed = 0
    for line in src.split('\n'):
        if line.lstrip().startswith('#include') or 'NX_TEST_SECTION' in line or 'raw-utf8' in line \
           or 'std::cout' in line or 'std::string (' in line or 'R"(' in line:
            out.append(line); continue
        def rep(m):
            global changed
            body = m.group(1)
            if any(ord(ch) > 127 for ch in body):
                return '"' + body + '"_t'
            return m.group(0)
        code, comment = line, ''
        idx = line.find('//')
        if idx >= 0 and line[:idx].count('"') % 2 == 0:
            code, comment = line[:idx], line[idx:]
        new = lit.sub(rep, code) + comment
        if new != line: changed += 1
        out.append(new)
    if changed:
        text = '\n'.join(out)
        if '#include "' in text and 'Text.h"' not in text and not path.endswith('Text.h'):
            pass
        open(path, 'w', encoding='utf-8').write(text)
        print(path, changed)
