// VendorMenuResearch.java
// ---------------------------------------------------------------------
// Ghidra-Skript zur Suche der nativen Vendor-Menue-UI-Funktion in Cube.exe
// (CubeWorld Alpha, 32-bit). Folgt dem im ROADMAP.md beschriebenen Muster
// von AdventureModResearch.java (mehrere klar getrennte "Abschnitte", die
// gezielt Einzelfragen beantworten, statt eines monolithischen Scans).
//
// AUSGANGSLAGE (siehe AdventureMod/README.md, "Bekannter offener Punkt:
// natives Shop-UI"):
//   Live-Test zeigt: hostility_flags=3, char_class=0x84, movement_flags-
//   Gate-Bit geloescht -- alle von uns dokumentierten Vorbedingungen fuer
//   FUN_004882e0 sind erfuellt, UND der vendor_session_pool hat freie Slots
//   (backref=0x0), UND laut Decompile (Abschnitt A) sollte FUN_004882e0
//   unter diesen Bedingungen einen freien Slot claimen. Trotzdem bleibt der
//   Pool nach [R]-Druck leer -- das native Shop-Menu oeffnet sich nicht.
//   FUN_00488ee0 (die groessere Funktion, die [R]-Tasten-Interaktionen
//   dispatcht und FUN_004882e0 aufruft) konnte im letzten Lauf NICHT
//   dekompiliert werden ("Decompile fehlgeschlagen", vermutlich Timeout bei
//   77332 Bytes Funktionsgroesse). Abschnitte E/F/G unten zoomen gezielt an
//   die beiden bekannten Aufrufstellen heran (0x4906d5, 0x496a4f, siehe
//   Abschnitt B des vorherigen Laufs), statt die ganze Funktion zu
//   dekompilieren.
//
// VERWENDUNG:
//   1. Cube.exe in Ghidra importieren/analysieren (falls noch nicht
//      geschehen) -- Imagebase sollte 0x00400000 sein (Standard bei
//      diesem Build, siehe AdventureMod/README.md Offset-Tabelle).
//   2. Dieses Skript in den Ghidra Script Manager laden (Ordner
//      hinzufuegen: AdventureMod/ghidra_scripts) oder headless ausfuehren:
//        analyzeHeadless <projekt-pfad> <projekt-name> -process Cube.exe
//          -scriptPath AdventureMod/ghidra_scripts -postScript VendorMenuResearch.java
//   3. Ergebnisse erscheinen in der Ghidra-Konsole UND werden zusaetzlich
//      als Textdatei "vendor_menu_research_output.txt" neben dem Skript-
//      Ausfuehrungsort abgelegt (siehe writeReportLine()/REPORT_PATH).
//
// EHRLICHER HINWEIS: Dieses Skript automatisiert nur das *Suchen* von
// Kandidaten (Xrefs, Strings, Immediate-Operanden, Disassembly-Fenster) --
// es identifiziert die Ziel-Funktion NICHT zuverlaessig automatisch. Die
// Ausgabe muss von Hand in Ghidra durchgesehen werden.
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class VendorMenuResearch extends GhidraScript {

	// -----------------------------------------------------------------
	// Bekannte, im Projekt bereits verifizierte/vermutete Adressen
	// (siehe AdventureMod/README.md Offset-Tabelle und ROADMAP.md).
	// Alle Adressen sind bereits ABSOLUT (Imagebase 0x00400000 eingerechnet)
	// -- passe sie an, falls deine Ghidra-Instanz die .exe an einer anderen
	// Adresse geladen hat.
	// -----------------------------------------------------------------
	private static final String ADDR_FUN_004882E0 = "004882e0"; // Slot-Suche/-Zuweisung, "[R] Talk"-Handler-Teil 2
	private static final String ADDR_FUN_00488EE0 = "00488ee0"; // grosse Dispatch-Funktion, ruft FUN_004882e0 auf
	private static final String ADDR_FUN_004E4A20 = "004e4a20"; // Namensregistrierung/-lookup ("innkeeper"-Template)
	private static final String ADDR_FUN_004E4BD0 = "004e4bd0"; // rollt Waren-Items fuer den Vendor-Slot

	// NEU (Abschnitt H) -- gefunden per Disassembly-Fenster um die erste
	// FUN_004882e0-Aufrufstelle (Abschnitt E, 0x4906d5): BEVOR FUN_004882e0
	// dort ueberhaupt aufgerufen wird, liefert FUN_0042c6c0 ein "aktuelles
	// Interaktionsziel"-Objekt (in ESI), dessen Feld ESI+0x18 auf 0 geprueft
	// wird (bei 0: kompletter Talk-Zweig wird uebersprungen!) und dessen
	// Feld ESI+0x25 den Interaktionstyp waehlt (1=Talk, 2=?, 5=Ride). Danach
	// wird ESI+0x8 (vermutlich eine GUID) per FUN_0042f000 GEGEN &gc->world
	// nachgeschlagen -- schlaegt dieser Lookup fehl (EAX==0), wird
	// FUN_004882e0 ebenfalls NICHT aufgerufen. Das ist ein sehr
	// wahrscheinlicher Kandidat dafuer, warum unser Haendler trotz korrekter
	// hostility_flags/char_class/movement_flags nie einen Vendor-Pool-Slot
	// bekommt: wir erreichen FUN_004882e0 auf diesem Pfad moeglicherweise
	// gar nicht erst.
	private static final String ADDR_FUN_0042C6C0 = "0042c6c0"; // liefert "aktuelles Interaktionsziel"-Objekt
	private static final String ADDR_FUN_0042F000 = "0042f000"; // GUID-Lookup gegen World (vermutlich EntitesMap)

	// NEU (Abschnitt I) -- gefunden per 150-Instruktionen-Disassembly-Fenster
	// um Aufrufstelle #2 (Abschnitt E, 0x496a4f): "CMP byte ptr [0x76b138],0x0;
	// JNZ ..." blockiert bei Nicht-Null die GESAMTE Talk-Interaktionslogik.
	// Absolute Adresse, ImageBase 0x400000 -> Offset 0x36b138.
	private static final String ADDR_GLOBAL_BLOCK_FLAG = "0076b138";

	// NEU (Abschnitt L-Fund) -- Live-Test zeigte: char_class=0x84 (unser
	// Haendler) wird in FUN_004882e0 fest verdrahtet als "Innkeeper"
	// behandelt (nur Rast-Dialogtext). Abschnitt L fand einen ZWEITEN,
	// aehnlichen hartcodierten Klassenwert: CMP byte ptr [ESI+0x140],0x89
	// in FUN_004889e0 (Adresse 0048b954) -- starker Kandidat fuer den
	// "echter Vendor/Shop"-Sonderfall.
	private static final String ADDR_FUN_004889E0 = "004889e0";

	// NEU (Abschnitt M-Fund, Nutzerwunsch "eigene Items ins Waren-Inventar"):
	// exakte Aufrufstelle 00488799 in FUN_004882e0 zeigt die echten Argumente
	// fuer FUN_004e4bd0: this=(gc+0x314), param_1=(gc+0x2e4). Der eine
	// beobachtete Waren-Eintrag ist aber ein zusammengesetzter "random:N"-
	// Container (Creature+Food+Monster+ItemVendorItem gemeinsam verpackt),
	// KEIN einzelnes Item. Der eigentliche Baustein fuer ein einzelnes Item
	// ist FUN_00595010 (wird in FUN_004e4bd0 fuer alle "itemvendoritem"-
	// Faelle mit nur 2 Bytes category_id/item_id aufgerufen). Diese und ihre
	// Hilfsfunktionen (Name-String-Init, Container-Init) dekompilieren, um
	// den Aufruf mit unseren eigenen Item-Werten nachzubilden.
	private static final String ADDR_FUN_00595010 = "00595010"; // baut EIN Item aus category_id/item_id
	private static final String ADDR_FUN_0040EB60 = "0040eb60"; // Name-String in Container schreiben (z.B. L"itemvendoritem")
	private static final String ADDR_FUN_0042F3E0 = "0042f3e0"; // initialisiert das 2-Byte item-spec Scratchpad
	private static final String ADDR_FUN_00593E50 = "00593e50"; // "committet" einen Container-Eintrag
	private static final String ADDR_FUN_00630A10 = "00630a10"; // erzeugt leeren Ausgabe-Container (lokal_1c/local_34/local_18 Pattern)

	// NEU (nach FUN_00595010-Analyse): letzte fehlende Glieder, um ein via
	// FUN_00595010 gebautes Item-Ergebnis SICHER in den bereits offenen
	// wares-Vektor unserer Session (+0x160) zu verschieben, ohne den
	// kompletten Slot-Claim-Ablauf von FUN_004882e0 erneut anzustossen.
	private static final String ADDR_FUN_0046D520 = "0046d520"; // initialisiert Akkumulator-Sentinel (local_24[0]/local_60[0]/local_58 Pattern)
	private static final String ADDR_FUN_004681E0 = "004681e0"; // verschiebt Akkumulator in den echten wares-Vektor (iVar7+0x160)

	// NEU (Abschnitt N-Fund, Live-Test-Crash beim [K]-Waren-Injection-
	// Versuch): FUN_00595010 ruft intern FUN_0059fbf0 (Key-Aufbau aus
	// category_id/item_id) und FUN_005a02d0((this+0x30), key) (Tree-Lookup)
	// auf. FUN_005a02d0 uebergibt IRGENDEINEN "this" an die RB-Tree-Find-
	// Funktion FUN_004e3790, die dort mit ungueltigem Zeiger abstuerzt --
	// wir kennen den Offset/die Umformung zwischen "worldCtx+0x30" und dem
	// tatsaechlich erwarteten Tree-Objekt nicht. Dekompilieren, um die
	// fehlende Verbindung zu finden.
	private static final String ADDR_FUN_0059FBF0 = "0059fbf0"; // baut Lookup-Key aus category_id/item_id
	private static final String ADDR_FUN_005A02D0 = "005a02d0"; // Tree-Lookup-Wrapper (ruft FUN_004e3790 auf)

	// Bekannte Aufrufstellen von FUN_004882e0 INNERHALB von FUN_00488ee0
	// (aus Abschnitt B des vorherigen Laufs). Abschnitt E disassembliert
	// ein Fenster VOR jeder dieser Adressen, um die Bedingung(en) zu sehen,
	// die den jeweiligen Aufruf gaten.
	private static final String[] CALLSITES_IN_00488EE0 = { "004906d5", "00496a4f" };

	// Such-Strings fuer moegliche Shop-/Vendor-UI-Assets (siehe ROADMAP.md
	// und generische CubeWorld-Alpha-Asset-Namensmuster: *.png/*.cub fuer
	// UI-Icons bzw. 3D-Schilder).
	private static final String[] UI_STRING_HINTS = {
		"buy", "sell", "vendor", "shop", "trade", "merchant", "wares",
		"innkeeper", "buy-back", "icon-vendor", "shop-sign"
	};

	// Immediate-Operand, von dem laut ROADMAP.md bekannt ist, dass er das
	// Waren-Inventar-Feld eines Vendor-Slots adressiert (slot+0x160).
	private static final long OFFSET_SLOT_WARES = 0x160;
	// Backref-Feld eines Vendor-Slots (slot+0x188) -- als zweiter Anker,
	// um Treffer zu filtern, die tatsaechlich zum Vendor-Slot-Layout passen.
	private static final long OFFSET_SLOT_BACKREF = 0x188;

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_menu_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorMenuResearch.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			sectionA_DecompileKnownFunctions();
			sectionB_CallersOfSlotSearch();
			sectionC_StringHints();
			sectionD_ImmediateOperandScan();
			sectionE_DisassembleAroundCallsites();
			sectionF_RetryDecompile00488ee0();
			sectionG_KeyRInputHandler();
			sectionH_DecompileInteractionTargetLookup();
			sectionI_GlobalBlockFlagXrefs();
			sectionJ_GlobalBlockFlagInstructionWindows();
			sectionK_InteractionTypeWriteScan();
			sectionL_CharClassSpecialCaseScan();
			sectionM_WaresRollCallsiteWindows();
			sectionN_CrashAddressLookup();
			sectionO_UiStringReadSite();
			sectionP_WaresPlusDisplayCacheScan();
			sectionQ_FindOperatorNewAddress();
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Abschnitt A: die vier bekannten Kern-Funktionen dekompilieren und in
	// den Bericht schreiben, damit man sie ohne Ghidra-GUI nachlesen kann.
	// -----------------------------------------------------------------
	private void sectionA_DecompileKnownFunctions() throws Exception {
		writeReportLine("--- Abschnitt A: Bekannte Kern-Funktionen (Decompile) ---");
		decompileAndReport("FUN_004882e0 (Slot-Suche/-Zuweisung)", ADDR_FUN_004882E0, 60);
		decompileAndReport("FUN_00488ee0 (Dispatch, ruft FUN_004882e0)", ADDR_FUN_00488EE0, 60);
		decompileAndReport("FUN_004e4a20 (Namensregistrierung/-lookup)", ADDR_FUN_004E4A20, 60);
		decompileAndReport("FUN_004e4bd0 (Waren-Roll-Funktion)", ADDR_FUN_004E4BD0, 60);
		decompileAndReport("FUN_004889e0 (Abschnitt-L-Fund: char_class==0x89 Sonderfall)", ADDR_FUN_004889E0, 60);
		decompileAndReport("FUN_00595010 (Abschnitt-M-Fund: baut EIN Item aus category_id/item_id)", ADDR_FUN_00595010, 60);
		decompileAndReport("FUN_0040eb60 (Name-String in Container schreiben)", ADDR_FUN_0040EB60, 60);
		decompileAndReport("FUN_0042f3e0 (item-spec Scratchpad initialisieren)", ADDR_FUN_0042F3E0, 60);
		decompileAndReport("FUN_00593e50 (Container-Eintrag committen)", ADDR_FUN_00593E50, 60);
		decompileAndReport("FUN_00630a10 (leeren Ausgabe-Container erzeugen)", ADDR_FUN_00630A10, 60);
		decompileAndReport("FUN_0046d520 (Akkumulator-Sentinel initialisieren)", ADDR_FUN_0046D520, 60);
		decompileAndReport("FUN_004681e0 (Akkumulator in echten wares-Vektor verschieben)", ADDR_FUN_004681E0, 60);
		decompileAndReport("FUN_0059fbf0 (Abschnitt-N-Fund: Lookup-Key aus category_id/item_id)", ADDR_FUN_0059FBF0, 60);
		decompileAndReport("FUN_005a02d0 (Abschnitt-N-Fund: Tree-Lookup-Wrapper)", ADDR_FUN_005A02D0, 60);
		writeReportLine("");
	}

	private void decompileAndReport(String label, String addrStr, int timeoutSeconds) throws Exception {
		Address addr = toAddr(addrStr);
		Function fn = getFunctionAt(addr);
		writeReportLine(">> " + label + " @ " + addrStr);
		if (fn == null) {
			writeReportLine("   (keine Funktion an dieser Adresse gefunden -- Adresse/Imagebase pruefen!)");
			writeReportLine("");
			return;
		}
		DecompileResults res = decompiler.decompileFunction(fn, timeoutSeconds, TaskMonitor.DUMMY);
		if (res != null && res.decompileCompleted()) {
			writeReportLine(res.getDecompiledFunction().getC());
		} else {
			String errMsg = (res != null) ? res.getErrorMessage() : "(keine Decompile-Ergebnis-Instanz)";
			writeReportLine("   (Decompile fehlgeschlagen -- Timeout=" + timeoutSeconds + "s, Fehler: " + errMsg + ")");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt B: alle Aufrufer (Xrefs) von FUN_004882e0 auflisten. Der
	// bekannte Aufrufer ist FUN_00488ee0 -- interessant sind ZUSAETZLICHE
	// Aufrufer, die bisher nicht dokumentiert wurden (moegliche zweite
	// Codepfad, der tatsaechlich einen Slot alloziert, bevor FUN_004882e0
	// ihn nur noch befuellt).
	// -----------------------------------------------------------------
	private void sectionB_CallersOfSlotSearch() throws Exception {
		writeReportLine("--- Abschnitt B: Aufrufer (Xrefs) von FUN_004882e0 ---");
		Address target = toAddr(ADDR_FUN_004882E0);
		ReferenceManager refMgr = currentProgram.getReferenceManager();
		ReferenceIterator refs = refMgr.getReferencesTo(target);
		int count = 0;
		while (refs.hasNext()) {
			Reference ref = refs.next();
			Address from = ref.getFromAddress();
			Function containing = getFunctionContaining(from);
			String fnName = (containing != null) ? containing.getName() + " @ " + containing.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  Xref von " + from + " in " + fnName + " (RefType=" + ref.getReferenceType() + ")");
			count++;
		}
		if (count == 0) {
			writeReportLine("  (keine Xrefs gefunden -- ungewoehnlich, Adresse/Analyse pruefen)");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt C: definierte Strings im Programm nach Vendor/Shop-UI-
	// Hinweisen durchsuchen und deren Xrefs (= vermutlich UI-Zeichen-Code)
	// auflisten.
	// -----------------------------------------------------------------
	private void sectionC_StringHints() throws Exception {
		writeReportLine("--- Abschnitt C: String-Hinweise auf Vendor-/Shop-UI ---");
		Listing listing = currentProgram.getListing();
		int totalHits = 0;

		for (Data data : getDefinedStrings(listing)) {
			String value;
			try {
				Object obj = data.getValue();
				value = (obj != null) ? obj.toString() : null;
			} catch (Exception e) {
				continue;
			}
			if (value == null) continue;

			String lower = value.toLowerCase();
			for (String hint : UI_STRING_HINTS) {
				if (lower.contains(hint)) {
					totalHits++;
					writeReportLine("  String @ " + data.getAddress() + ": \"" + value + "\" (Treffer: \"" + hint + "\")");
					ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(data.getAddress());
					while (refs.hasNext()) {
						Reference ref = refs.next();
						Address from = ref.getFromAddress();
						Function containing = getFunctionContaining(from);
						String fnName = (containing != null) ? containing.getName() + " @ " + containing.getEntryPoint() : "(keine Funktion)";
						writeReportLine("      benutzt in " + fnName + " (Instruktion @ " + from + ")");
					}
					break; // nicht mehrfach fuer mehrere Hints in einem String loggen
				}
			}
		}

		if (totalHits == 0) {
			writeReportLine("  (keine passenden Strings gefunden -- evtl. sind UI-Strings nicht als");
			writeReportLine("   Ghidra-\"String\"-Daten erkannt; alternativ Rohdaten-Suche noetig.)");
		}
		writeReportLine("");
	}

	// Iteriert ueber alle definierten Daten im Programm und sammelt jene mit
	// String-Wert (egal ob als "string"-Datentyp erkannt oder nur per
	// hasStringValue() -- deckt z.B. auch char-Arrays ab, die Ghidra nicht
	// als eigenen String-Typ markiert hat).
	private List<Data> getDefinedStrings(Listing listing) {
		List<Data> result = new ArrayList<>();
		ghidra.program.model.listing.DataIterator it = listing.getDefinedData(currentProgram.getMinAddress(), true);
		while (it.hasNext()) {
			Data d = it.next();
			if (d == null) continue;
			DataType dt = d.getDataType();
			boolean looksLikeString = (dt != null && dt.getName().toLowerCase().contains("string")) || d.hasStringValue();
			if (looksLikeString) {
				result.add(d);
			}
		}
		return result;
	}

	// -----------------------------------------------------------------
	// Abschnitt D: Instruktionen mit Immediate-Operand 0x160 (Waren-Feld
	// eines Vendor-Slots) UND separat 0x188 (Backref-Feld) auflisten.
	// Funktionen, die BEIDE Offsets referenzieren, sind starke Kandidaten
	// dafuer, mit dem Vendor-Slot-Layout zu arbeiten (Lese- ODER Render-Code).
	// -----------------------------------------------------------------
	private void sectionD_ImmediateOperandScan() throws Exception {
		writeReportLine("--- Abschnitt D: Instruktionen mit Vendor-Slot-Offsets (0x160 / 0x188) ---");

		java.util.Set<Function> functionsWithWares = new java.util.HashSet<>();
		java.util.Set<Function> functionsWithBackref = new java.util.HashSet<>();

		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						Function fn = getFunctionContaining(instr.getAddress());
						if (fn == null) continue;
						if (val == OFFSET_SLOT_WARES) {
							functionsWithWares.add(fn);
						} else if (val == OFFSET_SLOT_BACKREF) {
							functionsWithBackref.add(fn);
						}
					}
				}
			}
		}

		writeReportLine("  Funktionen, die Immediate 0x160 referenzieren (" + functionsWithWares.size() + "):");
		for (Function fn : functionsWithWares) {
			boolean alsoBackref = functionsWithBackref.contains(fn);
			writeReportLine("    " + fn.getName() + " @ " + fn.getEntryPoint()
				+ (alsoBackref ? "   <-- referenziert AUCH 0x188 (starker Kandidat!)" : ""));
		}

		writeReportLine("");
		writeReportLine("  Funktionen, die Immediate 0x188 referenzieren (" + functionsWithBackref.size() + "):");
		for (Function fn : functionsWithBackref) {
			writeReportLine("    " + fn.getName() + " @ " + fn.getEntryPoint());
		}

		writeReportLine("");
		writeReportLine("  HINWEIS: Diese Immediate-Werte kommen in vielen anderen, unabhaengigen");
		writeReportLine("  Strukturen im Programm ebenfalls vor (0x160/0x188 sind kleine, generische");
		writeReportLine("  Offsets). Die Trefferliste ist ein Startpunkt fuer manuelle Pruefung in der");
		writeReportLine("  Ghidra-Decompile-Ansicht, kein Beweis.");
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt E: Disassembly-Fenster VOR jeder bekannten Aufrufstelle von
	// FUN_004882e0 innerhalb von FUN_00488ee0. Ziel: die Bedingungssprünge
	// (Jcc-Instruktionen) unmittelbar vor jedem CALL zu sehen, um
	// herauszufinden, WELCHE zusaetzliche Bedingung (jenseits von
	// hostility_flags/char_class/movement_flags-Gate, die laut Live-Test
	// bereits alle erfuellt sind) den Aufruf noch blockieren koennte --
	// z.B. eine Entfernungspruefung, ein Cooldown, ein UI-Zustands-Flag
	// (Inventar/Chat/Menu bereits offen), oder eine Tastendruck-Erkennung,
	// die nicht direkt auf key_R (GameController.input.key_R) zugreift.
	// -----------------------------------------------------------------
	private void sectionE_DisassembleAroundCallsites() throws Exception {
		writeReportLine("--- Abschnitt E: Disassembly um die FUN_004882e0-Aufrufstellen in FUN_00488ee0 ---");
		for (String addrStr : CALLSITES_IN_00488EE0) {
			// Fenstergroesse deutlich vergroessert (war 50): wir suchen jetzt
			// gezielt danach, WAS [ESP+0x174] befuellt (die Quelle des in
			// Abschnitt H analysierten "Interaktionsziel"-Zeigers) -- das lag
			// ausserhalb des urspruenglichen 50-Instruktionen-Fensters.
			writeReportLine(">> Fenster um Aufrufstelle @ " + addrStr + " (150 Instruktionen davor, 10 danach):");
			Address callAddr = toAddr(addrStr);
			printInstructionWindow(callAddr, 150, 10);
			writeReportLine("");
		}
	}

	// Gibt ein Fenster von Instruktionen um eine Zieladresse herum aus.
	// "before"/"after" sind Instruktions-ANZAHL, nicht Byte-Anzahl (per
	// getPrevious()/getNext() ermittelt, damit Instruktionsgrenzen exakt
	// eingehalten werden statt mitten in einem Opcode zu landen).
	private void printInstructionWindow(Address target, int before, int after) {
		Instruction targetInstr = currentProgram.getListing().getInstructionAt(target);
		if (targetInstr == null) {
			writeReportLine("  (keine Instruktion an " + target + " gefunden -- Adresse pruefen)");
			return;
		}

		List<Instruction> window = new ArrayList<>();
		Instruction cur = targetInstr;
		for (int i = 0; i < before && cur != null; i++) {
			cur = cur.getPrevious();
			if (cur != null) window.add(0, cur);
		}
		window.add(targetInstr);
		cur = targetInstr;
		for (int i = 0; i < after && cur != null; i++) {
			cur = cur.getNext();
			if (cur != null) window.add(cur);
		}

		for (Instruction instr : window) {
			String marker = instr.getAddress().equals(target) ? "  >>> " : "      ";
			writeReportLine(marker + instr.getAddress() + ":  " + instr.toString());
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt F: erneuter Decompile-Versuch fuer FUN_00488ee0. STANDARDMAESSIG
	// DEAKTIVIERT (siehe kAttemptFullDecompile00488ee0) -- selbst mit 3000s
	// Timeout schlug der Versuch live fehl (vermutlich haengt sich der
	// Decompiler an dieser Funktion konsistent auf, nicht nur ein einfaches
	// Zeitproblem). Abschnitt E (gezielte Disassembly-Fenster) plus Abschnitt
	// H/I (gezielte Decompiles der tatsaechlich relevanten Unter-Funktionen)
	// haben sich als der zuverlaessige Ersatz erwiesen -- deshalb spart
	// dieser Schalter Zeit bei jedem weiteren Skriptlauf. Auf true setzen,
	// um es trotzdem nochmal zu versuchen (z.B. nach einem Ghidra-Update).
	// -----------------------------------------------------------------
	private static final boolean kAttemptFullDecompile00488ee0 = false;

	private void sectionF_RetryDecompile00488ee0() throws Exception {
		writeReportLine("--- Abschnitt F: FUN_00488ee0 erneut dekompilieren ---");
		if (!kAttemptFullDecompile00488ee0) {
			writeReportLine("  UEBERSPRUNGEN (kAttemptFullDecompile00488ee0=false) -- schlug bereits mit");
			writeReportLine("  60s UND 3000s Timeout fehl, vermutlich kein reines Zeitproblem. Siehe");
			writeReportLine("  Abschnitt E/H/I fuer den erfolgreichen Ersatz-Ansatz.");
			writeReportLine("");
			return;
		}
		decompileAndReport("FUN_00488ee0 (Retry, langes Timeout)", ADDR_FUN_00488EE0, 300);
	}

	// -----------------------------------------------------------------
	// Abschnitt G: Sucht Xrefs auf GameController.input.key_R (das per
	// cwsdk/GameController.h dokumentierte Byte-Feld fuer den R-Tasten-
	// Status) UND versucht, die Struct-Offset-Adresse dafuer zu berechnen,
	// um Funktionen zu finden, die aktiv auf R-Tastendruck reagieren --
	// als unabhaengiger Weg, die tatsaechliche [R]-Dispatch-Funktion zu
	// bestaetigen (sollte FUN_00488ee0 sein, aber schadet nicht, das
	// gegenzupruefen, falls doch eine andere/zusaetzliche Funktion beteiligt
	// ist, z.B. ein separater "Interaction-Prompt-Anzeigen"-Pfad vs.
	// "Interaction-Ausloesen"-Pfad).
	//
	// HINWEIS: GameController.input.key_R liegt laut cwsdk/GameController.h
	// bei Offset 0xB (11) INNERHALB des "input"-Unterstructs, der seinerseits
	// bei GameController-Offset 0x4 beginnt (nach dem vtable-Zeiger) --
	// also GameController+0xF. Das ist eine STRUCT-INSTANZ (kein separater
	// Zeiger), daher gibt es dafuer keine direkten "Daten-Xrefs" wie bei
	// einer globalen Variable -- stattdessen wird nach Instruktionen
	// gesucht, die exakt den Immediate-Offset 0xF referenzieren UND einen
	// Vergleich mit einem kleinen Wert (typischerweise 0/1 fuer
	// Tastendruck-Status) unmittelbar danach haben. Das ist notgedrungen
	// unscharf (0xF ist ein sehr generischer, kleiner Offset) -- Ergebnis
	// ist ein grober Kandidaten-Pool, kein Beweis.
	// -----------------------------------------------------------------
	private void sectionG_KeyRInputHandler() throws Exception {
		writeReportLine("--- Abschnitt G: Grobe Suche nach key_R-Verwendung (GameController+0xF) ---");
		writeReportLine("  HINWEIS: 0xF ist ein sehr generischer Offset -- viele Treffer sind falsch-positiv.");
		writeReportLine("  Nur als grober Kandidaten-Pool gedacht, siehe Abschnitt-Kommentar im Code.");

		final long KEY_R_OFFSET = 0xF;
		java.util.Set<Function> functionsUsingOffset = new java.util.HashSet<>();

		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						if (val == KEY_R_OFFSET) {
							Function fn = getFunctionContaining(instr.getAddress());
							if (fn != null) functionsUsingOffset.add(fn);
						}
					}
				}
			}
		}

		writeReportLine("  Funktionen, die Immediate 0xF referenzieren (" + functionsUsingOffset.size() + "):");
		int printed = 0;
		for (Function fn : functionsUsingOffset) {
			writeReportLine("    " + fn.getName() + " @ " + fn.getEntryPoint()
				+ (fn.getEntryPoint().toString().replace("0x", "").equalsIgnoreCase("00488ee0") ? "   <-- ist FUN_00488ee0!" : ""));
			printed++;
			if (printed >= 100) {
				writeReportLine("    ... (Ausgabe nach 100 Treffern abgeschnitten, zu generischer Offset)");
				break;
			}
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt H: Dekompiliert die beiden Funktionen, die laut Abschnitt E
	// VOR FUN_004882e0 gaten, ob der Talk-Zweig ueberhaupt erreicht wird:
	// FUN_0042c6c0 (liefert das "aktuelle Interaktionsziel"-Objekt, dessen
	// Felder +0x18/+0x25/+0x8 die Weichen stellen) und FUN_0042f000 (der
	// GUID-Lookup gegen &gc->world, der bei Fehlschlag den Talk-Zweig
	// ebenfalls abbricht). Beide Funktionen sind vermutlich deutlich
	// kleiner als FUN_00488ee0 und sollten ohne Timeout-Probleme
	// dekompilieren.
	// -----------------------------------------------------------------
	private void sectionH_DecompileInteractionTargetLookup() throws Exception {
		writeReportLine("--- Abschnitt H: Interaktionsziel-Lookup (Gate vor FUN_004882e0) ---");
		decompileAndReport("FUN_0042c6c0 (liefert Interaktionsziel-Objekt, Felder +0x18/+0x25/+0x8 pruefen)", ADDR_FUN_0042C6C0, 60);
		decompileAndReport("FUN_0042f000 (GUID-Lookup gegen &gc->world)", ADDR_FUN_0042F000, 60);
	}

	// -----------------------------------------------------------------
	// Abschnitt I: Xrefs auf das globale Blockier-Flag bei 0x76b138 (siehe
	// Abschnitt E-Fund). Ziel: herausfinden, WER dieses Byte auf ungleich 0
	// setzt (und damit die gesamte Talk-Interaktion blockiert) -- z.B. ein
	// anderes offenes Menu, ein Cooldown, oder ein Ladezustand.
	// -----------------------------------------------------------------
	private void sectionI_GlobalBlockFlagXrefs() throws Exception {
		writeReportLine("--- Abschnitt I: Xrefs auf globales Blockier-Flag (0x76b138) ---");
		Address flagAddr = toAddr(ADDR_GLOBAL_BLOCK_FLAG);
		ReferenceManager refMgr = currentProgram.getReferenceManager();
		ReferenceIterator refs = refMgr.getReferencesTo(flagAddr);
		int count = 0;
		while (refs.hasNext()) {
			Reference ref = refs.next();
			Address from = ref.getFromAddress();
			Function containing = getFunctionContaining(from);
			String fnName = (containing != null) ? containing.getName() + " @ " + containing.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  Xref von " + from + " in " + fnName + " (RefType=" + ref.getReferenceType() + ")");
			count++;
		}
		if (count == 0) {
			writeReportLine("  (keine Xrefs gefunden -- evtl. ist die Adresse in dieser Ghidra-Analyse");
			writeReportLine("   nicht als Daten-Label erkannt; pruefe die Adresse manuell im Listing.)");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt J: Live-Test (siehe README.md) zeigt das Flag bei 0x76b138
	// DAUERHAFT auf 1, waehrend [R] gehalten wird UND obwohl unser Haendler
	// bereits einen vendor_session_pool-Slot hat (FUN_004882e0 wurde also via
	// Aufrufstelle #1 schon erfolgreich aufgerufen). Aufrufstelle #2 verlangt
	// laut Abschnitt E aber Flag==0, um ueberhaupt weiterzumachen -- das Flag
	// haengt vermutlich fest auf 1 und blockiert dadurch den Pfad, der das
	// eigentliche UI oeffnet/cached_vendor_session setzt.
	//
	// Ziel dieses Abschnitts: fuer jede der 6 aus Abschnitt I bekannten
	// Adressen ein kleines Instruktionsfenster ausgeben, um zu sehen:
	//   (a) SETZT der Write bei 0049b596 das Flag auf 1 oder LOESCHT er es
	//       (auf 0)? (MOV byte ptr [0x76b138], <imm> -- Immediate ablesen)
	//   (b) liegt dieser Write VOR oder NACH den beiden bekannten
	//       Aufrufstellen (004906d5 / 00496a4f)?
	//   (c) sind die 5 Reads alle einfache "CMP ...,0 / JNZ" wie beim ersten
	//       Fund, oder unterscheiden sie sich (z.B. anderer Vergleichswert)?
	// -----------------------------------------------------------------
	private static final String[] BLOCK_FLAG_XREF_ADDRS = {
		"0049699e", "00496af5", "00497376", "0049798e", "00497dbc", "0049b596"
	};

	private void sectionJ_GlobalBlockFlagInstructionWindows() throws Exception {
		writeReportLine("--- Abschnitt J: Instruktionsfenster um alle Xrefs auf 0x76b138 ---");
		for (String addrStr : BLOCK_FLAG_XREF_ADDRS) {
			writeReportLine(">> Fenster um " + addrStr + " (15 Instruktionen davor, 8 danach):");
			Address a = toAddr(addrStr);
			printInstructionWindow(a, 15, 8);
			writeReportLine("");
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt K: Live-Test zeigt, dass Aufrufstelle #1 (die den Vendor-
	// Slot tatsaechlich claimt, siehe Abschnitt E @ 004906d5) NICHT an
	// key_R haengt, sondern jeden Frame fuer das aktuell anvisierte Ziel
	// laeuft -- und dabei ESI+0x25==1 verlangt ("Interaktionstyp=Talk").
	// Live-Test: mal wird der Slot geclaimt (voriger Lauf, R gehalten),
	// mal nicht (dieser Lauf, kurzer Tap) -- OBWOHL hostility_flags/
	// char_class/movement_flags immer korrekt sind. Verdacht: +0x25 ist
	// ein SEPARATES "NPC-Rolle/Dialogtyp"-Feld (nicht hostility_flags),
	// das bei echten Vendor-NPCs ueber ihr Spawn-Template gesetzt wird,
	// aber in unserem manuellen SpawnCreature()-Pfad nie initialisiert
	// wird (daher zufaelliger Speicherinhalt statt zuverlaessig 1).
	//
	// Dieser Abschnitt listet alle MOV-Instruktionen mit Immediate-
	// Operand 0x25 auf (Kandidaten fuer "schreibe ins +0x25-Feld"), damit
	// wir von Hand pruefen koennen, welche Funktion(en) es auf 1 setzen
	// und unter welcher Bedingung (z.B. beim NPC-Template-Laden).
	// -----------------------------------------------------------------
	private void sectionK_InteractionTypeWriteScan() throws Exception {
		writeReportLine("--- Abschnitt K: MOV-Instruktionen mit Immediate 0x25 (Interaktionstyp-Feld) ---");
		writeReportLine("  Gesucht: Stellen, die vermutlich ESI+0x25 (Interaktionstyp) auf 1 (Talk) setzen.");
		writeReportLine("  Filter: Mnemonic beginnt mit MOV UND Instruktion referenziert Immediate 0x25.");
		writeReportLine("");

		int count = 0;
		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			String mnem = instr.getMnemonicString();
			if (!mnem.startsWith("MOV")) continue;

			boolean hasOffset25 = false;
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						if (val == 0x25) {
							hasOffset25 = true;
						}
					}
				}
			}
			if (!hasOffset25) continue;

			Function fn = getFunctionContaining(instr.getAddress());
			String fnName = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  " + instr.getAddress() + " in " + fnName + ":  " + instr.toString());
			count++;
			if (count >= 150) {
				writeReportLine("  ... (Ausgabe nach 150 Treffern abgeschnitten)");
				break;
			}
		}
		if (count == 0) {
			writeReportLine("  (keine Treffer -- Filter evtl. zu eng, Offset manuell in Ghidra pruefen)");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt L: DURCHBRUCH per Live-Test -- ein kurzer [R]-Tap DIREKT
	// auf den Haendler geblickt loest tatsaechlich Dialog aus ("You can
	// rest in our inn between six o'clock...")! Das ist die generische
	// INNKEEPER-Rast-Zeile. In FUN_004882e0 (Abschnitt A) gibt es genau
	// diesen Vergleich:
	//   if (*(char *)(param_1 + 0x140) == -0x7c) { ...
	//       FUN_0040f7a0(&local_38, L"innkeeper", 9);
	//       FUN_004e4a20(...); }
	// -0x7c (signed byte) == 0x84 (unsigned) == UNSER char_class! Das
	// Spiel behandelt char_class=0x84 fest verdrahtet als "Innkeeper"
	// (nur Rast-Dialog, KEIN Buy/Sell-Fenster) -- die gesamte Talk-Pipeline
	// (Slot-Claim, Waren-Rollen, Dialogtext) funktioniert also bereits
	// einwandfrei, nur die falsche NPC-Rolle wird ausgeloest.
	//
	// Dieser Abschnitt sucht alle CMP/TEST-Instruktionen mit Immediate-
	// Operand 0x140 (der char_class-Offset) im ganzen Programm, um WEITERE
	// fest verdrahtete Klassenwerte zu finden (z.B. echte Haendler-/
	// Schmied-/Ladenbesitzer-Klassen, die tatsaechlich ein Shop-Fenster
	// statt Dialogtext ausloesen).
	// -----------------------------------------------------------------
	private void sectionL_CharClassSpecialCaseScan() throws Exception {
		writeReportLine("--- Abschnitt L: CMP-Instruktionen mit Immediate 0x140 (char_class-Offset) ---");
		writeReportLine("  Gesucht: weitere fest verdrahtete char_class-Sonderfaelle (Vorbild: 0x84=Innkeeper).");
		writeReportLine("  Filter: Mnemonic beginnt mit CMP/TEST UND Instruktion referenziert Immediate 0x140.");
		writeReportLine("");

		int count = 0;
		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			String mnem = instr.getMnemonicString();
			if (!mnem.startsWith("CMP") && !mnem.startsWith("TEST")) continue;

			boolean hasOffset140 = false;
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						if (val == 0x140) {
							hasOffset140 = true;
						}
					}
				}
			}
			if (!hasOffset140) continue;

			Function fn = getFunctionContaining(instr.getAddress());
			String fnName = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  " + instr.getAddress() + " in " + fnName + ":  " + instr.toString());
			count++;
			if (count >= 150) {
				writeReportLine("  ... (Ausgabe nach 150 Treffern abgeschnitten)");
				break;
			}
		}
		if (count == 0) {
			writeReportLine("  (keine Treffer -- Filter evtl. zu eng, Offset manuell in Ghidra pruefen)");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt M (Nutzerwunsch "Waren-Inventar mit eigenen Items fuellen"):
	// Live-Test zeigt, dass das Waren-Feld (Vendor-Slot +0x160) kein
	// flaches Item-Array ist, sondern ein Vektor von 8-Byte-Eintraegen
	// {void* itemPtr; int32 qty}, wobei itemPtr auf ein komplexes,
	// baumartiges "Container"-Objekt zeigt (mehrere interne Zeiger, keine
	// einfache Item-Struct-Kopie) -- von Hand nachbauen waere riskant
	// (Absturzgefahr bei falschem Layout).
	//
	// Sicherer Weg: denselben nativen Aufrufpfad benutzen, den auch
	// FUN_004e4bd0 fuer die "itemvendoritem"-Faelle benutzt (FUN_0040eb60 +
	// FUN_00595010 + FUN_00593e50 + FUN_004e4a20), aber mit unserer
	// eigenen category_id/item_id statt eines Zufallswerts.
	//
	// Dafuer muessen wir wissen, WELCHE Werte FUN_004882e0 beim Aufruf von
	// FUN_004e4bd0(this, param_1, param_2, param_3, param_4) tatsaechlich
	// uebergibt (this/param_1/param_4 sind die kritischen Kontext-Zeiger,
	// die wir fuer einen eigenen Aufruf ebenfalls bereitstellen muessten).
	// Dieser Abschnitt disassembliert das Umfeld jeder Aufrufstelle von
	// FUN_004e4bd0, damit wir von Hand sehen koennen, aus welchen
	// Feldern/Registern diese Argumente stammen.
	// -----------------------------------------------------------------
	private void sectionM_WaresRollCallsiteWindows() throws Exception {
		writeReportLine("--- Abschnitt M: Aufrufstellen von FUN_004e4bd0 (Waren-Roll) disassembliert ---");
		writeReportLine("  Ziel: herausfinden, was this/param_1/param_4 beim Aufruf sind, um denselben");
		writeReportLine("  Aufruf spaeter selbst mit eigener category_id/item_id nachzubilden.");
		writeReportLine("");

		Address target = toAddr(ADDR_FUN_004E4BD0);
		ReferenceManager refMgr = currentProgram.getReferenceManager();
		ReferenceIterator refs = refMgr.getReferencesTo(target);
		int count = 0;
		while (refs.hasNext()) {
			Reference ref = refs.next();
			Address from = ref.getFromAddress();
			Function containing = getFunctionContaining(from);
			String fnName = (containing != null) ? containing.getName() + " @ " + containing.getEntryPoint() : "(keine Funktion)";
			writeReportLine(">> Aufrufstelle @ " + from + " in " + fnName + " (RefType=" + ref.getReferenceType() + "):");
			printInstructionWindow(from, 25, 3);
			writeReportLine("");
			count++;
		}
		if (count == 0) {
			writeReportLine("  (keine Xrefs gefunden -- ungewoehnlich, Adresse/Analyse pruefen)");
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Abschnitt N (Nutzerwunsch, Live-Test-Crash beim [K]-Waren-Injection-
	// Versuch): SEH-Filter in TryInjectCustomWares() meldete Absturz bei
	// statischer Adresse 0x4e3797 -- das liegt NICHT innerhalb von
	// FUN_00595010 selbst (die faengt erst bei 0x595010 an), sondern
	// vermutlich in einer der von FUN_00595010 aufgerufenen Unterfunktionen
	// (FUN_0059fbf0, FUN_005a02d0, FUN_004517d0, FUN_00451800, FUN_004da1a0,
	// FUN_004689a0, FUN_0040f680) oder ist eine spaeter manifestierende
	// Heap-Korruption. Dieser Abschnitt identifiziert die Funktion an der
	// Absturzadresse und dekompiliert sie, damit wir sehen, was dort
	// tatsaechlich passiert.
	// -----------------------------------------------------------------
	private static final String ADDR_CRASH_004E3797 = "004e3797";

	private void sectionN_CrashAddressLookup() throws Exception {
		writeReportLine("--- Abschnitt N: Funktion an Absturzadresse 0x4e3797 (Live-Test-Crash [K]) ---");
		Address crashAddr = toAddr(ADDR_CRASH_004E3797);
		Function fn = getFunctionContaining(crashAddr);
		if (fn == null) {
			writeReportLine("  (keine Funktion an dieser Adresse gefunden -- Adresse pruefen)");
			writeReportLine("");
			return;
		}
		writeReportLine("  Absturzadresse liegt in Funktion: " + fn.getName() + " @ " + fn.getEntryPoint());
		writeReportLine("");
		writeReportLine("  Disassembly-Fenster um die genaue Absturzinstruktion:");
		printInstructionWindow(crashAddr, 20, 5);
		writeReportLine("");
		decompileAndReport("Funktion an Absturzadresse (" + fn.getName() + ")", fn.getEntryPoint().toString(), 60);
	}

	// -----------------------------------------------------------------
	// Abschnitt O (DURCHBRUCH per Cheat-Engine-Speicher-Breakpoint):
	// Live-Test zeigte, dass session+0x160 ("wares") sich zwischen JEDER
	// NPC-Interaktion aendert (auch beim Wechsel zwischen unserem Haendler
	// und einem echten Vendor) UND dass alle Haendler sich dasselbe
	// Rueckkauf-Menu teilen -- starker Hinweis, dass wares+0x160 NICHT die
	// tatsaechliche Kauf-Liste ist, die die UI rendert. Per Cheat Engine
	// wurde der String "fair Cotton Boots" (ein echtes, sichtbares Shop-Item)
	// im Speicher gesucht und ein "Wer liest diese Adresse"-Breakpoint
	// gesetzt. Ergebnis: CALL-Instruktion bei Cube.exe+28E1E0 (Imagebase
	// 0x400000 -> absolute Adresse 0x68E1E0), die Cube.exe+28E7A1 (absolut
	// 0x68E7A1) aufruft. Dieser Abschnitt dekompiliert BEIDE Funktionen
	// (die aufrufende UND die aufgerufene) sowie deren Aufrufer (Xrefs), um
	// die tatsaechliche UI-Populate-Funktion zu finden -- das ist die neue,
	// vielversprechendste Spur, seit wir FUN_004882e0 flaeschlich fuer die
	// Waren-Fuell-Funktion gehalten haben.
	// -----------------------------------------------------------------
	private static final String ADDR_UI_STRING_READ_SITE = "0068e1e0";   // CALL-Instruktion, liest "fair Cotton Boots"
	private static final String ADDR_UI_STRING_READ_TARGET = "0068e7a1"; // aufgerufene Funktion

	private void sectionO_UiStringReadSite() throws Exception {
		writeReportLine("--- Abschnitt O: Cheat-Engine-Fund -- UI liest Item-Namen bei 0x68e1e0 (ruft 0x68e7a1) ---");

		Address callSite = toAddr(ADDR_UI_STRING_READ_SITE);
		Function callerFn = getFunctionContaining(callSite);
		if (callerFn == null) {
			writeReportLine("  (keine Funktion an 0x68e1e0 gefunden -- Imagebase pruefen! Cheat Engine zeigt");
			writeReportLine("   MODUL-relative Adressen (\"Cube.exe+X\"), Ghidra braucht die ABSOLUTE Adresse");
			writeReportLine("   NUR wenn Ghidras Imagebase ebenfalls 0x400000 ist, ist Modul-Offset==Ghidra-Offset.");
			writeReportLine("   Falls nicht: Imagebase in Ghidra pruefen (Window -> Program Trees, oder");
			writeReportLine("   currentProgram.getImageBase() -- steht oben im Bericht) und Adresse neu");
			writeReportLine("   umrechnen: Ghidra-Adresse = Ghidra-Imagebase + 0x28e1e0.");
		} else {
			writeReportLine("  Aufrufende Funktion (enthaelt den CALL bei 0x68e1e0): " + callerFn.getName()
				+ " @ " + callerFn.getEntryPoint());
		}
		writeReportLine("");
		writeReportLine("  Disassembly-Fenster um 0x68e1e0 (60 Instruktionen davor, 15 danach) -- zeigt");
		writeReportLine("  hoffentlich, WOHER der Item-Name-Zeiger/Item-Struct-Zeiger kommt (Register-Setup");
		writeReportLine("  vor dem CALL) und WAS mit dem Ergebnis passiert (UI-Widget-Text setzen?):");
		printInstructionWindow(callSite, 60, 15);
		writeReportLine("");

		if (callerFn != null) {
			decompileAndReport("Aufrufende Funktion (enthaelt CALL @ 0x68e1e0)", callerFn.getEntryPoint().toString(), 90);

			writeReportLine("  Aufrufer (Xrefs) der aufrufenden Funktion " + callerFn.getName() + ":");
			ReferenceManager refMgr = currentProgram.getReferenceManager();
			ReferenceIterator callerRefs = refMgr.getReferencesTo(callerFn.getEntryPoint());
			int count = 0;
			while (callerRefs.hasNext()) {
				Reference ref = callerRefs.next();
				Address from = ref.getFromAddress();
				Function containing = getFunctionContaining(from);
				String fnName = (containing != null) ? containing.getName() + " @ " + containing.getEntryPoint() : "(keine Funktion)";
				writeReportLine("    Xref von " + from + " in " + fnName + " (RefType=" + ref.getReferenceType() + ")");
				count++;
			}
			if (count == 0) {
				writeReportLine("    (keine Xrefs gefunden)");
			}
			writeReportLine("");
		}

		decompileAndReport("Aufgerufene Funktion @ 0x68e7a1", ADDR_UI_STRING_READ_TARGET, 60);
	}

	// -----------------------------------------------------------------
	// Abschnitt P (Cheat-Engine-Sackgasse -- der angezeigte Item-Name wird
	// offenbar prozedural zusammengesetzt, nicht als fertiger String
	// gehalten, daher per Speicher-Scan nicht auffindbar): im bereits
	// vorhandenen Decompile von FUN_004882e0 (Abschnitt A) faellt auf, dass
	// die Session NEBEN wares (+0x160) noch ein zweites Feld-Paar +0x178/
	// +0x17c hat (ein weiterer Vektor, 0x18=24 Byte pro Eintrag), das bei
	// jeder Waren-Aenderung GELEERT wird:
	//   for (puVar11 = *(iVar7+0x178); puVar11 != *(iVar7+0x17c); puVar11+=6) { ... }
	//   *(iVar7+0x17c) = *(iVar7+0x178);
	// Starker Verdacht: das ist ein separater "Anzeige-Cache" (UI-Widget-
	// Liste), der aus wares NEU AUFGEBAUT werden muss, von einer Funktion,
	// die wir noch nicht kennen -- unser [K]/[L] loest diesen Rebuild nie
	// aus, weil wir nur FUN_004881e0 (wares ersetzen) + das Leeren von
	// +0x178/+0x17c nachbauen, aber nie den eigentlichen Rebuild-Schritt.
	// Dieser Abschnitt sucht gezielt Funktionen, die BEIDE Offsets (0x160
	// UND 0x178) referenzieren -- viel enger als der Abschnitt-D-Scan (der
	// nur 0x160/0x188 kombinierte), sollte die echte Rebuild-Funktion
	// isolieren.
	// -----------------------------------------------------------------
	private static final long OFFSET_SESSION_DISPLAY_CACHE = 0x178;

	private void sectionP_WaresPlusDisplayCacheScan() throws Exception {
		writeReportLine("--- Abschnitt P: Funktionen, die SOWOHL 0x160 ALS AUCH 0x178 referenzieren ---");
		writeReportLine("  Ziel: die Funktion finden, die aus wares(+0x160) den Anzeige-Cache (+0x178/");
		writeReportLine("  +0x17c) NEU AUFBAUT (nicht nur leert wie FUN_004882e0) -- das waere die echte");
		writeReportLine("  \"Shop-Liste rendern/aktualisieren\"-Funktion.");
		writeReportLine("");

		java.util.Set<Function> functionsWithWares = new java.util.HashSet<>();
		java.util.Set<Function> functionsWithDisplayCache = new java.util.HashSet<>();

		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						Function fn = getFunctionContaining(instr.getAddress());
						if (fn == null) continue;
						if (val == OFFSET_SLOT_WARES) {
							functionsWithWares.add(fn);
						} else if (val == OFFSET_SESSION_DISPLAY_CACHE) {
							functionsWithDisplayCache.add(fn);
						}
					}
				}
			}
		}

		writeReportLine("  Funktionen mit BEIDEN Offsets (0x160 UND 0x178):");
		int hitCount = 0;
		for (Function fn : functionsWithWares) {
			if (functionsWithDisplayCache.contains(fn)) {
				writeReportLine("    " + fn.getName() + " @ " + fn.getEntryPoint());
				hitCount++;
			}
		}
		if (hitCount == 0) {
			writeReportLine("    (keine Treffer)");
		}
		writeReportLine("");

		// Fuer jeden Treffer AUSSER FUN_004882e0 selbst (die kennen wir
		// schon) gleich mitdekompilieren, damit wir sofort sehen, was dort
		// passiert.
		for (Function fn : functionsWithWares) {
			if (functionsWithDisplayCache.contains(fn) && !fn.getEntryPoint().toString().equalsIgnoreCase(ADDR_FUN_004882E0)) {
				decompileAndReport("Kandidat fuer Anzeige-Rebuild (" + fn.getName() + ")", fn.getEntryPoint().toString(), 60);
			}
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt Q (DURCHBRUCH per FUN_004e5f90-Decompile aus Abschnitt P):
	// FUN_004e5f90 ist die RENDER-Funktion der Shop-Liste. Sie liest fertige
	// Zeilen aus einem Array bei session+0x178/+0x17c -- laut Cleanup-Code
	// in FUN_004882e0/FUN_00488030 (puVar11[5]=Kapazitaet, puVar11[4]=Laenge,
	// *(undefined2*)puVar11=erstes Zeichen -- klassisches MSVC-wstring-
	// Layout, 0x18 Byte pro Eintrag, FLACHES Array KEIN Sentinel-Header wie
	// bei wares) -- ein Array fertiger std::wstring-Objekte, EINE Zeile Text
	// pro Item. Wir bauen dieses Array jetzt SELBST (per FUN_0040eb60, das
	// wir schon sicher benutzen), um die native "Populate"-Funktion zu
	// umgehen, die vermutlich nur im echten [R]-Interaktionspfad laeuft.
	// Dafuer brauchen wir die ECHTE Adresse von "operator new" (NICHT
	// unseren eigenen DLL-Heap!), damit spaeteres natives "operator delete"
	// beim Session-Cleanup nicht auf einem fremden Heap abstuerzt. Dieser
	// Abschnitt sucht alle Symbole/Funktionen mit "operator" im Namen.
	// -----------------------------------------------------------------
	private void sectionQ_FindOperatorNewAddress() throws Exception {
		writeReportLine("--- Abschnitt Q: Suche nach operator-new/operator-delete-Adressen ---");
		writeReportLine("  Ziel: die ECHTE Adresse von \"operator new\" (fuer sicheres Allozieren im");
		writeReportLine("  Spiel-Heap), um das +0x178/+0x17c-Anzeige-Array selbst zu bauen.");
		writeReportLine("");

		ghidra.program.model.symbol.SymbolTable symTable = currentProgram.getSymbolTable();
		ghidra.program.model.symbol.SymbolIterator symIter = symTable.getSymbolIterator();
		int count = 0;
		while (symIter.hasNext()) {
			ghidra.program.model.symbol.Symbol sym = symIter.next();
			String name = sym.getName();
			String lower = name.toLowerCase();
			if (lower.contains("operator") && (lower.contains("new") || lower.contains("delete"))) {
				writeReportLine("  Symbol: " + name + " @ " + sym.getAddress()
					+ "  (Typ=" + sym.getSymbolType() + ")");
				count++;
			}
		}
		if (count == 0) {
			writeReportLine("  (keine Symbole mit \"operator\"+\"new\"/\"delete\" im Namen gefunden --");
			writeReportLine("   evtl. sind sie nur als anonyme Thunk-Funktionen ohne Namen vorhanden.");
			writeReportLine("   Alternativ: Funktion an der Aufrufstelle in FUN_0046d520 (Abschnitt A,");
			writeReportLine("   ruft laut Decompile \"operator_new(0x28)\" auf) direkt disassemblieren,");
			writeReportLine("   um die Zieladresse des CALLs zu sehen.)");
			writeReportLine("");
			writeReportLine("  Disassembly von FUN_0046d520 (zeigt den echten CALL zu operator new):");
			Address fn520 = toAddr(ADDR_FUN_0046D520);
			printInstructionWindow(fn520, 2, 15);
		}
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	private void writeReportLine(String line) {
		println(line);
		try {
			report.write(line);
			report.newLine();
		} catch (IOException e) {
			println("Fehler beim Schreiben in Report-Datei: " + e.getMessage());
		}
	}
}
