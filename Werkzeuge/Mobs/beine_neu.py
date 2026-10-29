# beine_neu.py (2026-10-29): NUR die Bein-Teile (fuesse/haende) der Tiere aus den Generatoren neu schreiben
# (Originalform, wie tierwelt.py / tiere_neu2.py sie bauen). .mob bleiben unangetastet.
#   python beine_neu.py [Strukturen]
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import tierbau as T
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'Strukturen')
DONE = []
def mob_legs_only(name, disp, base, factor, parts, host=1, note='', extra=()):
    for k in (T.HANDS, T.FEET):
        p = parts.get(k)
        if p is None or p == 'base': continue
        rel = os.path.join('Teile', T.CFG['part_dir'], "%s-%s.cub" % (name, T.SLOTN[k])); p.save(rel)
    DONE.append(name); return 0
def run(script, stop):
    src = open(os.path.join(HERE, script), encoding='utf-8').read()
    src = src[:src.index(stop)] if stop else src
    sys.argv = [script, OUT]; g = {'__name__': 'beine', '__file__': os.path.join(HERE, script)}
    exec(compile(src, script, 'exec'), g)
T.mob = mob_legs_only; T.OUT = OUT
run('tierwelt.py', '# Liste fuer den Spawner')
T.OUT = OUT; T.MOBS.clear(); T.CFG.update({'race_base': 200, 'mob_dir': 'Tiere', 'part_dir': 'Tiere'})
run('tiere_neu2.py', 'names = [m[0] for m in T.MOBS]')
print(len(DONE), 'Tiere: Beine neu')
