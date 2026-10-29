# Tiergruppen (2026-10-16): je Medium 5 Gruppen. Biome II kann ganze Gruppen spawnen lassen ("@land/raubtiere").
# Schluessel = Gruppenkennung, Wert = (Anzeigename, Tiere). Tiere aus Strukturen\Mobs\Tiere stehen als "..\Tiere\name".
GROUPS = {
  'land': [
    ('raubtiere', 'Raubtiere', 'wolf_grau loewe gepard hyaene tiger panther_schwarz schneeleopard ..\\Tiere\\braunbaer ..\\Tiere\\eisbaer ..\\Tiere\\schwarzbaer ..\\Tiere\\luchs ..\\Tiere\\fuchs ..\\Tiere\\polarfuchs'),
    ('pflanzenfresser', 'Pflanzenfresser & Huftiere', 'wildschwein elch wisent reh zebra giraffe gnu strauss warzenschwein antilope kamel gorilla faultier tapir steinbock yak mammut rentier ..\\Tiere\\elefant ..\\Tiere\\hirsch ..\\Tiere\\hirschkuh ..\\Tiere\\nashorn'),
    ('kleintiere', 'Kleintiere', 'dachs igel eichhoernchen hase waschbaer erdmaennchen fennek guerteltier murmeltier sumpfratte ameisenbaer'),
    ('kriechtiere', 'Reptilien, Amphibien & Krabbler', 'skorpion waran klapperschlange chamaeleon pfeilgiftfrosch riesenkroete schnappschildkroete'),
    ('fantasie', 'Fantasiewesen', 'einhorn basilisk kristallkaefer pilzling moosgolem glutsalamander schattenwolf ..\\Tiere\\pegasus ..\\Tiere\\zentaur ..\\Tiere\\schlangenmensch ..\\Tiere\\geist ..\\Voelker\\titan-hyperion ..\\Voelker\\titan-okeanos ..\\Voelker\\titan-kronos ..\\Voelker\\titan-atlas ..\\Voelker\\titan-prometheus ..\\Voelker\\kull-krieger ..\\Voelker\\kull-haeuptling ..\\Voelker\\kull-schamane ..\\Voelker\\kull-berserker ..\\Voelker\\kull-waechter'),
  ],
  'luft': [
    ('greifvoegel', 'Greifvoegel & Eulen', 'eule uhu geier kondor bussard milan seeadler schneeeule ..\\Tiere\\adler ..\\Tiere\\falke'),
    ('singvoegel', 'Sing-, Raben- & Buntvoegel', 'rabe kraehe elster taube spatz rotkehlchen specht eisvogel ara tukan kakadu wellensittich ..\\Tiere\\kolibri ..\\Tiere\\schwalbe'),
    ('wasservoegel', 'Wasser- & Watvoegel', 'storch kranich reiher pelikan moewe albatros kormoran schwan wildgans wildente flamingo'),
    ('insekten', 'Insekten & Fledertiere', 'fledermaus flughund motte biene hummel hornisse zikade gluehkaefer ..\\Tiere\\gluehwuermchen ..\\Tiere\\libelle ..\\Tiere\\marienkaefer ..\\Tiere\\monarchfalter ..\\Tiere\\schmetterling'),
    ('fantasie', 'Fantasiewesen & Drachen', 'phoenix greif wyvern federschlange himmelsrochen luftqualle lichtfee windgeist kristallfalter funkenvogel sturmvogel ..\\Tiere\\frostdrache ..\\Tiere\\giftdrache ..\\Tiere\\golddrache'),
  ],
  'wasser': [
    ('raubfische', 'Raubfische & Haie', 'hecht barsch wels piranha zitteraal barrakuda thunfisch schwertfisch muraene anglerfisch hammerhai weisser_hai'),
    ('friedfische', 'Friedfische', 'forelle lachs karpfen goldfisch koi axolotl clownfisch doktorfisch kugelfisch seepferdchen mondfisch rochen manta'),
    ('meeressaeuger', 'Meeressaeuger & Riesen', 'walhai delfin orca buckelwal narwal blauwal seehund seeloewe walross seeotter seekuh pinguin'),
    ('krebse', 'Krebse, Kraken & Reptilien', 'seestern krabbe hummer nautilus krake kalmar qualle meeresschildkroete krokodil wasserschlange'),
    ('fantasie', 'Fantasiewesen', 'seeschlange riesenkraken kristallqualle'),
  ],
}
# Die 26 Tiere aus Strukturen\Mobs\Tiere fuer die Tierwelt-Liste: datei;medium;lebensraeume;gewicht;gruppe_min;gruppe_max
OLD = [
  ('..\\Tiere\\adler', 'luft', 'gebirge,wald', 6, 1, 1), ('..\\Tiere\\braunbaer', 'land', 'wald,gebirge', 6, 1, 1), ('..\\Tiere\\eisbaer', 'land', 'schnee', 6, 1, 1),
  ('..\\Tiere\\elefant', 'land', 'savanne', 6, 2, 4), ('..\\Tiere\\falke', 'luft', 'wiese,gebirge', 6, 1, 1), ('..\\Tiere\\frostdrache', 'luft', 'schnee,fantasie', 1, 1, 1),
  ('..\\Tiere\\fuchs', 'land', 'wald,wiese', 8, 1, 2), ('..\\Tiere\\geist', 'land', 'sumpf,fantasie', 2, 1, 1), ('..\\Tiere\\giftdrache', 'luft', 'sumpf,fantasie', 1, 1, 1),
  ('..\\Tiere\\gluehwuermchen', 'luft', 'wald,sumpf', 8, 3, 6), ('..\\Tiere\\golddrache', 'luft', 'gebirge,fantasie', 1, 1, 1), ('..\\Tiere\\hirsch', 'land', 'wald', 8, 1, 2),
  ('..\\Tiere\\hirschkuh', 'land', 'wald,wiese', 8, 1, 3), ('..\\Tiere\\kolibri', 'luft', 'dschungel', 8, 1, 3), ('..\\Tiere\\libelle', 'luft', 'sumpf', 8, 1, 3),
  ('..\\Tiere\\luchs', 'land', 'wald,schnee', 6, 1, 1), ('..\\Tiere\\marienkaefer', 'luft', 'wiese', 8, 2, 4), ('..\\Tiere\\monarchfalter', 'luft', 'wiese', 8, 2, 4),
  ('..\\Tiere\\nashorn', 'land', 'savanne', 6, 1, 2), ('..\\Tiere\\pegasus', 'land', 'wiese,fantasie', 2, 1, 1), ('..\\Tiere\\polarfuchs', 'land', 'schnee', 8, 1, 2),
  ('..\\Tiere\\schlangenmensch', 'land', 'dschungel,fantasie', 2, 1, 2), ('..\\Tiere\\schmetterling', 'luft', 'wiese,wald', 8, 2, 4), ('..\\Tiere\\schwalbe', 'luft', 'wiese', 8, 2, 4),
  ('..\\Tiere\\schwarzbaer', 'land', 'wald', 6, 1, 1), ('..\\Tiere\\zentaur', 'land', 'wiese,fantasie', 2, 1, 3),
  # Voelker (titanen_kull.py): ersetzen am nativen Spawn Kyklopen (Titanen) bzw. Oger (Kull) -- gleiche Basisrasse
  ('..\\Voelker\\titan-hyperion', 'land', 'wueste,savanne,gebirge', 4, 1, 1), ('..\\Voelker\\titan-okeanos', 'land', 'kueste,sumpf', 4, 1, 1),
  ('..\\Voelker\\titan-kronos', 'land', 'gebirge,schnee', 4, 1, 1), ('..\\Voelker\\titan-atlas', 'land', 'gebirge,wiese', 4, 1, 1),
  ('..\\Voelker\\titan-prometheus', 'land', 'wueste,gebirge', 4, 1, 1),
  ('..\\Voelker\\kull-krieger', 'land', 'wald,gebirge,wiese,sumpf', 8, 1, 3), ('..\\Voelker\\kull-haeuptling', 'land', 'gebirge,wald', 3, 1, 1),
  ('..\\Voelker\\kull-schamane', 'land', 'sumpf,wald', 3, 1, 1), ('..\\Voelker\\kull-berserker', 'land', 'wald,wiese,savanne', 5, 1, 2),
  ('..\\Voelker\\kull-waechter', 'land', 'gebirge,schnee', 4, 1, 2),
]
def group_of(name, medium):
    for key, disp, members in GROUPS[medium]:
        if name in members.split(): return key
    return ''
def check(names_by_medium):
    """Jedes Tier genau einer Gruppe seines Mediums -- Fehlerliste."""
    err = []
    for med, names in names_by_medium.items():
        for n in names:
            hits = [k for k, d, m in GROUPS[med] if n in m.split()]
            if len(hits) != 1: err.append((med, n, hits))
    return err
HEADER = ("# Tierwelt (LAB/Spawner): datei;medium(land/luft/wasser);lebensraeume (komma);gewicht;gruppe_min;gruppe_max;tiergruppe\n"
          "# Lebensraeume: wald wiese savanne wueste dschungel schnee gebirge sumpf kueste | suess riff meer tief | fantasie (selten ueberall)\n"
          "# Tiergruppen (Biome II: \"@medium/gruppe\"): " + " | ".join(med + ": " + ", ".join(k + "=" + d for k, d, _ in g) for med, g in GROUPS.items()) + "\n")
