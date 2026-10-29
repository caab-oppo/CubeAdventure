# Tierwelt (2026-10-14)

150 Tiere aus Grundformen, je eine eigene Rasse (226..375). Der Spawner (Wildlife.cpp) setzt sie nach Lebensraum um den Spieler; Wassertiere schwimmen (schwimmt=1), Flieger fliegen (fliegt=1).

| Mob | Name | Rasse | Basis | Medium | Lebensraum | Faktor |
|---|---|---|---|---|---|---|
| wildschwein | Wildschwein | 226 | Pig | land | wald | 1.3 |
| dachs | Dachs | 227 | Mole | land | wald, wiese | 0.8 |
| igel | Igel | 228 | Porcupine | land | wald, wiese | 0.45 |
| eichhoernchen | Eichhoernchen | 229 | Squirrel | land | wald | 0.45 |
| hase | Hase | 230 | Bunny | land | wiese, wald, savanne | 0.6 |
| waschbaer | Waschbaer | 231 | Raccoon | land | wald, sumpf | 0.7 |
| wolf_grau | Wolf | 232 | Wolf | land | wald, schnee | 1.15 |
| elch | Elch | 233 | Horse | land | wald, schnee | 1.6 |
| wisent | Wisent | 234 | Cow | land | wiese, wald | 1.5 |
| reh | Reh | 235 | Horse | land | wald, wiese | 0.85 |
| loewe | Loewe | 236 | Panther | land | savanne | 1.4 |
| zebra | Zebra | 237 | Horse | land | savanne | 1.2 |
| giraffe | Giraffe | 238 | Alpaca | land | savanne | 1.8 |
| gnu | Gnu | 239 | Cow | land | savanne | 1.4 |
| gepard | Gepard | 240 | Cat | land | savanne | 1.5 |
| hyaene | Hyaene | 241 | Wolf | land | savanne, wueste | 1.1 |
| strauss | Strauss | 242 | Chicken | land | savanne, wueste | 2.0 |
| warzenschwein | Warzenschwein | 243 | Pig | land | savanne | 1.0 |
| antilope | Antilope | 244 | Horse | land | savanne, wiese | 0.95 |
| erdmaennchen | Erdmaennchen | 245 | Squirrel | land | savanne, wueste | 0.4 |
| kamel | Kamel | 246 | Camel | land | wueste | 1.4 |
| fennek | Fennek | 247 | Collie | land | wueste | 0.5 |
| skorpion | Skorpion | 248 | Crab | land | wueste | 1.0 |
| guerteltier | Guerteltier | 249 | Porcupine | land | wueste, savanne | 0.55 |
| waran | Waran | 250 | Crocodile | land | wueste, dschungel | 1.4 |
| klapperschlange | Klapperschlange | 251 | Crocodile | land | wueste | 0.9 |
| tiger | Tiger | 252 | Panther | land | dschungel | 1.5 |
| panther_schwarz | Schwarzer Panther | 253 | Panther | land | dschungel | 1.5 |
| gorilla | Gorilla | 254 | Monkey | land | dschungel | 1.6 |
| faultier | Faultier | 255 | Koala | land | dschungel | 0.9 |
| tapir | Tapir | 256 | Pig | land | dschungel, sumpf | 1.3 |
| chamaeleon | Chamaeleon | 257 | Crocodile | land | dschungel | 0.4 |
| pfeilgiftfrosch | Pfeilgiftfrosch | 258 | Frog | land | dschungel | 0.3 |
| ameisenbaer | Ameisenbaer | 259 | Pig | land | dschungel, savanne | 1.3 |
| schneeleopard | Schneeleopard | 260 | Panther | land | schnee, gebirge | 1.5 |
| steinbock | Steinbock | 261 | Sheep | land | gebirge, schnee | 1.2 |
| yak | Yak | 262 | Cow | land | schnee, gebirge | 1.5 |
| murmeltier | Murmeltier | 263 | Squirrel | land | gebirge, wiese | 0.55 |
| mammut | Mammut | 264 | Mammoth | land | schnee | 2.2 |
| rentier | Rentier | 265 | Horse | land | schnee | 1.2 |
| riesenkroete | Riesenkroete | 266 | Frog | land | sumpf | 1.6 |
| schnappschildkroete | Schnappschildkroete | 267 | Turtle | land | sumpf | 1.4 |
| sumpfratte | Sumpfratte | 268 | Mole | land | sumpf | 0.6 |
| einhorn | Einhorn | 269 | Horse | land | wiese, wald, fantasie | 1.2 |
| basilisk | Basilisk | 270 | Crocodile | land | sumpf, dschungel, fantasie | 1.8 |
| kristallkaefer | Kristallkaefer | 271 | Crab | land | gebirge, fantasie | 1.1 |
| pilzling | Pilzling | 272 | Bunny | land | wald, sumpf, fantasie | 0.6 |
| moosgolem | Moosgolem-Schildkroete | 273 | Turtle | land | wald, sumpf, fantasie | 2.4 |
| glutsalamander | Glutsalamander | 274 | Crocodile | land | wueste, gebirge, fantasie | 1.1 |
| schattenwolf | Schattenwolf | 275 | Wolf | land | wald, schnee, fantasie | 1.4 |
| eule | Eule | 276 | Owl | luft | wald | 0.8 |
| uhu | Uhu | 277 | Owl | luft | wald, gebirge | 1.3 |
| rabe | Rabe | 278 | Crow | luft | wald, gebirge, schnee | 1.1 |
| kraehe | Kraehe | 279 | Crow | luft | wiese, wald | 0.8 |
| elster | Elster | 280 | Crow | luft | wiese, wald | 0.75 |
| taube | Taube | 281 | Crow | luft | wiese, wald | 0.6 |
| spatz | Spatz | 282 | Crow | luft | wiese, wald | 0.35 |
| rotkehlchen | Rotkehlchen | 283 | Crow | luft | wald, wiese | 0.35 |
| specht | Specht | 284 | Crow | luft | wald | 0.55 |
| eisvogel | Eisvogel | 285 | Crow | luft | wald, sumpf | 0.4 |
| storch | Storch | 286 | Crow | luft | wiese, sumpf | 1.2 |
| kranich | Kranich | 287 | Crow | luft | sumpf, wiese | 1.3 |
| reiher | Reiher | 288 | Crow | luft | sumpf, kueste | 1.3 |
| pelikan | Pelikan | 289 | Seagull | luft | kueste | 1.4 |
| moewe | Moewe | 290 | Seagull | luft | kueste | 0.8 |
| albatros | Albatros | 291 | Seagull | luft | kueste | 1.6 |
| kormoran | Kormoran | 292 | Seagull | luft | kueste, sumpf | 1.0 |
| schwan | Schwan | 293 | Seagull | luft | sumpf, wiese | 1.3 |
| wildgans | Wildgans | 294 | Seagull | luft | sumpf, wiese | 1.1 |
| wildente | Wildente | 295 | Seagull | luft | sumpf, wiese | 0.6 |
| geier | Geier | 296 | Crow | luft | wueste, savanne | 1.3 |
| kondor | Kondor | 297 | Crow | luft | gebirge | 1.8 |
| bussard | Bussard | 298 | Crow | luft | wiese, wald | 1.2 |
| milan | Milan | 299 | Crow | luft | wiese | 1.2 |
| ara | Ara | 300 | Parrot | luft | dschungel | 0.9 |
| tukan | Tukan | 301 | Parrot | luft | dschungel | 0.7 |
| kakadu | Kakadu | 302 | Parrot | luft | dschungel, savanne | 0.7 |
| wellensittich | Wellensittich | 303 | Parrot | luft | savanne, dschungel | 0.3 |
| flamingo | Flamingo | 304 | Crow | luft | sumpf, kueste | 1.4 |
| fledermaus | Fledermaus | 305 | Bat | luft | wald, gebirge, dschungel | 0.5 |
| flughund | Flughund | 306 | Bat | luft | dschungel | 1.1 |
| motte | Motte | 307 | Fly | luft | wald, wiese | 0.6 |
| biene | Biene | 308 | Bumblebee | luft | wiese, wald | 0.35 |
| hummel | Hummel | 309 | Bumblebee | luft | wiese | 0.5 |
| hornisse | Hornisse | 310 | Hornet | luft | wald | 0.6 |
| zikade | Zikade | 311 | Fly | luft | savanne, dschungel | 0.5 |
| gluehkaefer | Gluehkaefer | 312 | Fly | luft | sumpf, wald | 0.4 |
| phoenix | Phoenix | 313 | Dragon | luft | wueste, gebirge, fantasie | 1.6 |
| greif | Greif | 314 | Dragon | luft | gebirge, fantasie | 1.8 |
| wyvern | Wyvern | 315 | Dragon | luft | gebirge, sumpf, fantasie | 3.2 |
| federschlange | Federschlange | 316 | Dragon | luft | dschungel, fantasie | 1.8 |
| himmelsrochen | Himmelsrochen | 317 | Shark | luft | kueste, wiese, fantasie | 2.0 |
| luftqualle | Luftqualle | 318 | Shark | luft | sumpf, wald, fantasie | 1.6 |
| lichtfee | Lichtfee | 319 | Fly | luft | wald, fantasie | 0.4 |
| windgeist | Windgeist | 320 | Shark | luft | gebirge, schnee, fantasie | 1.8 |
| kristallfalter | Kristallfalter | 321 | Fly | luft | gebirge, fantasie | 0.7 |
| funkenvogel | Funkenvogel | 322 | Crow | luft | wueste, fantasie | 0.6 |
| seeadler | Seeadler | 323 | Crow | luft | kueste, gebirge | 1.5 |
| schneeeule | Schneeeule | 324 | Owl | luft | schnee | 1.0 |
| sturmvogel | Sturmvogel | 325 | Seagull | luft | kueste, gebirge, fantasie | 1.4 |
| forelle | Forelle | 326 | Shark | wasser | suess | 0.7 |
| lachs | Lachs | 327 | Shark | wasser | suess, meer | 0.9 |
| karpfen | Karpfen | 328 | Shark | wasser | suess | 0.9 |
| hecht | Hecht | 329 | Shark | wasser | suess | 1.2 |
| barsch | Barsch | 330 | Shark | wasser | suess | 0.6 |
| wels | Wels | 331 | Shark | wasser | suess | 1.4 |
| goldfisch | Goldfisch | 332 | Shark | wasser | suess | 0.35 |
| koi | Koi | 333 | Shark | wasser | suess | 0.8 |
| piranha | Piranha | 334 | Shark | wasser | suess | 0.45 |
| zitteraal | Zitteraal | 335 | Shark | wasser | suess, sumpf | 1.6 |
| axolotl | Axolotl | 336 | Shark | wasser | suess, sumpf | 0.45 |
| clownfisch | Clownfisch | 337 | Shark | wasser | riff | 0.3 |
| doktorfisch | Doktorfisch | 338 | Shark | wasser | riff | 0.45 |
| kugelfisch | Kugelfisch | 339 | Shark | wasser | riff, meer | 0.5 |
| seepferdchen | Seepferdchen | 340 | Shark | wasser | riff | 0.4 |
| seestern | Seestern | 341 | Shark | wasser | riff, meer | 0.5 |
| krabbe | Krabbe | 342 | Shark | wasser | riff, meer, suess | 0.6 |
| hummer | Hummer | 343 | Shark | wasser | meer, riff | 0.9 |
| nautilus | Nautilus | 344 | Shark | wasser | meer, tief | 0.7 |
| thunfisch | Thunfisch | 345 | Shark | wasser | meer | 1.2 |
| schwertfisch | Schwertfisch | 346 | Shark | wasser | meer | 1.4 |
| mondfisch | Mondfisch | 347 | Shark | wasser | meer, tief | 1.8 |
| barrakuda | Barrakuda | 348 | Shark | wasser | meer, riff | 1.4 |
| rochen | Rochen | 349 | Shark | wasser | meer, riff | 1.2 |
| manta | Manta | 350 | Shark | wasser | meer | 2.2 |
| anglerfisch | Anglerfisch | 351 | Shark | wasser | tief | 1.1 |
| hammerhai | Hammerhai | 352 | Shark | wasser | meer | 1.6 |
| weisser_hai | Weisser Hai | 353 | Shark | wasser | meer, tief | 2.0 |
| walhai | Walhai | 354 | Shark | wasser | meer, tief | 3.0 |
| delfin | Delfin | 355 | Shark | wasser | meer | 1.6 |
| orca | Orca | 356 | Shark | wasser | meer, tief | 2.2 |
| buckelwal | Buckelwal | 357 | Shark | wasser | tief, meer | 3.8 |
| narwal | Narwal | 358 | Shark | wasser | meer | 1.8 |
| seehund | Seehund | 359 | Shark | wasser | meer, kueste | 1.0 |
| seeloewe | Seeloewe | 360 | Shark | wasser | meer, kueste | 1.4 |
| walross | Walross | 361 | Shark | wasser | meer, kueste | 1.8 |
| seeotter | Seeotter | 362 | Shark | wasser | kueste, meer | 0.6 |
| seekuh | Seekuh | 363 | Shark | wasser | kueste, suess | 1.8 |
| krake | Krake | 364 | Shark | wasser | meer, riff | 1.4 |
| kalmar | Kalmar | 365 | Shark | wasser | meer, tief | 1.2 |
| qualle | Qualle | 366 | Shark | wasser | meer, riff | 0.7 |
| meeresschildkroete | Meeresschildkroete | 367 | Shark | wasser | meer, riff | 1.3 |
| krokodil | Krokodil | 368 | Shark | wasser | sumpf, suess | 1.6 |
| wasserschlange | Wasserschlange | 369 | Shark | wasser | riff, meer, sumpf | 1.0 |
| pinguin | Pinguin | 370 | Shark | wasser | meer, kueste | 0.6 |
| blauwal | Blauwal | 371 | Shark | wasser | tief | 4.5 |
| muraene | Muraene | 372 | Shark | wasser | riff | 1.3 |
| seeschlange | Seeschlange | 373 | Shark | wasser | meer, tief, fantasie | 2.8 |
| riesenkraken | Riesenkraken | 374 | Shark | wasser | tief, fantasie | 3.5 |
| kristallqualle | Kristallqualle | 375 | Shark | wasser | tief, meer, fantasie | 1.4 |
