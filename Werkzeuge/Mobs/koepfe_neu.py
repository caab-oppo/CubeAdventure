# koepfe_neu.py (2026-10-27): NUR die Koepfe mit Hals (hals=1: Huftiere auf Pferd/Alpaka-Skelett, Drachen/Greife) neu bauen.
# Die .mob bleiben (Ausrichtung!) -- nur Kopf-.cub ueberschreiben und hals=1 eintragen. Danach: tierbild.py --ausrichten --schreiben
#   python koepfe_neu.py [Strukturen]
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import tierbau as T, tierbild as TB
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'Strukturen')
DONE = []
HEAD_SCALE_DRAGON = 0.9
def mob_head_only(name, disp, base, factor, parts, host=1, note='', extra=()):
    if 'hals=1' not in extra: return 0
    rel = os.path.join('Teile', T.CFG['part_dir'], f"{name}-kopf.cub"); parts[T.HEAD].save(rel)
    mp = os.path.join(T.OUT, 'Mobs', T.CFG['mob_dir'], name + '.mob')
    m = TB.read_mob(mp); TB.set_kv(m, 'hals', 1)
    if base == 'Dragon' and T.HEAD in m['parts']: m['parts'][T.HEAD]['scale'] = HEAD_SCALE_DRAGON   # nativ 0.51 fuer den 44 lange Kopf
    TB.write_mob(m); DONE.append(name); return 0
def run(script, stop):
    src = open(os.path.join(HERE, script), encoding='utf-8').read()
    src = src[:src.index(stop)] if stop else src
    sys.argv = [script, OUT]; g = {'__name__': 'koepfe', '__file__': os.path.join(HERE, script)}
    exec(compile(src, script, 'exec'), g)
T.mob = mob_head_only
T.OUT = OUT
run('tierwelt.py', '# Liste fuer den Spawner')
T.OUT = OUT; T.MOBS.clear(); T.CFG.update({'race_base': 200, 'mob_dir': 'Tiere', 'part_dir': 'Tiere'})
run('tiere_neu2.py', 'names = [m[0] for m in T.MOBS]')
print(len(DONE), 'Koepfe neu:', ', '.join(DONE))
