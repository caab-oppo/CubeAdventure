# Sammelt alle Cube.exe-Adressen aus dem Quelltext (auch aus Kommentaren: lieber zu viele als zu wenige) (absolute 0x4xxxxx..0x79xxxx und "Host::ReOff(0x...)").
# Aufruf (im Ordner Projekt):  python Werkzeuge\RE\adressen_sammeln.py AdventureMod Werkzeuge\RE\adressen_cube.txt
import re, sys, glob, os
src, out = sys.argv[1], sys.argv[2]
s = set()
for f in [g for e in ('*.cpp', '*.h') for g in glob.glob(os.path.join(src, e)) + glob.glob(os.path.join(src, 'cwsdk', e))]:
    t = open(f, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'ReOff\((0x[0-9A-Fa-f]+)\)|_OFFSET\s*=\s*(0x[0-9A-Fa-f]+)', t): s.add(0x400000 + int(m.group(1) or m.group(2), 16))
    for m in re.finditer(r'\b0x([0-9A-Fa-f]{6,8})u?\b', t):
        v = int(m.group(1), 16)
        if 0x401000 <= v < 0x7A0000: s.add(v)
open(out, 'w').write('\n'.join('0x%06X' % v for v in sorted(s)) + '\n')
print(len(s), 'Adressen ->', out)
# Danach:  python Werkzeuge\RE\translate2.py Cube.exe Server.exe Werkzeuge\RE\adressen_cube.txt Werkzeuge\RE\adressen_server_ergebnis.txt AdventureMod\AddrMap_Server.inc
# (pip install capstone; Laufzeit ca. 3-5 Minuten)
