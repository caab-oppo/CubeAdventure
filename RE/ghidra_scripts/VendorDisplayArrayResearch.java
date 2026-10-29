// VendorDisplayArrayResearch.java
// ---------------------------------------------------------------------
// NEUE, AUFGERAEUMTE FASSUNG (Nachfolger der Version mit Abschnitt A-Z --
// die ist unuebersichtlich geworden und wird hier NICHT mehr fortgesetzt).
// Datei-/Klassenname bewusst UNVERAENDERT beibehalten (Ghidra hat bei
// Aenderung des Dateinamens Probleme beim Neustart gemacht).
//
// ==== STAND DER RECHERCHE (bestaetigte Fakten, siehe Chat-Verlauf) ====
//
// 1. Das native Buy/Sell-ICON-Fenster (Warengrid) ist ein KOMPLETT
//    ANDERES System als die NPC-Sprechblase (cube::SpeechWidget,
//    session+0x160 "wares" -- DAS ist per [K] von uns voll kontrollierbar,
//    siehe WanderingMerchant.cpp).
//
// 2. Das Icon-Fenster wird ueber DREI Kreatur-Felder gesteuert:
//      creature+0x1b0 = ZONEN-X-Koordinate
//      creature+0x1b4 = ZONEN-Y-Koordinate
//      creature+0x1b8 = Index/ID des Gebaeudes (zone_static_object) INNERHALB
//                        dieser Zone
//    Empirisch zweifelsfrei bestaetigt: zwei verschiedene echte Vendoren
//    hatten unterschiedliche, zu ihrer jeweiligen Zone passende Werte, UND
//    beide Werte stecken wortwoertlich (als Bytes) in der jeweiligen
//    Kreatur-GUID (die game-interne GUID wird offenbar aus der
//    Spawn-Zonen-Position gebildet).
//
// 3. FUN_00434d10(this, zoneX, zoneY) = World::GetZone(x,y) (Chunk-Lookup-
//    Arithmetik >>6 verifiziert per Decompile). Liefert einen Zeiger auf
//    die Zone-Struktur.
//
// 4. cube::Zone::static_objects ist ein Vektor von zone_static_object-
//    Eintraegen, JEWEILS 0x188 (388) BYTE GROSS (Stride per Disassembly
//    bestaetigt) -- die C++-Struct-Definition in cwsdk/cube/Zone.h /
//    non_cube.h deckt aber nur die ERSTEN ~0xC4 Byte ab! Es gibt also
//    einen GROSSEN UNKARTIERTEN BEREICH ab +0xC4 bis +0x188 pro Eintrag,
//    den wir noch nicht verstehen.
//
// 5. zone_static_object mit type==5 UND Eintrag+0x178==unserem creature+0x1b8
//    UND Eintrag+0x30-Flag!=0 ist der "Vendor-Gebaeude"-Eintrag (gefunden
//    in FUN_00595a60/FUN_005a4980, beide durchsuchen die Zone genau danach).
//    +0x178 liegt SELBST schon im unkartierten Bereich!
//
// 6. SACKGASSE (widerlegt): FUN_005a58e0/FUN_00468670/FUN_004dd700 (im
//    Treffer-Zweig von FUN_00595a60 aufgerufen) sind NUR Benachrichtigungs-
//    /Buchhaltungs-Code (Welt-Event-Liste, Kopie von Feldern in einen
//    lokalen Puffer, Einfuegen in eine verkettete Liste) -- KEINE
//    Item-Generierung. FUN_00595a60 selbst ist vermutlich eine "ist ein
//    Gebaeude in der Naehe"-Ambient-Scan-Funktion, nicht der eigentliche
//    [R]-Interaktions-Handler.
//
// ==== OFFENE FRAGE / ZIEL DIESES SKRIPTS ====
// Der eigentliche Icon-Fenster-Item-Generator wurde noch NICHT gefunden.
// Arbeitshypothese: er sitzt im unkartierten Bereich des
// zone_static_object-Eintrags (+0xC4 bis +0x188) -- entweder liest er
// direkt daraus (z.B. ein eingebetteter Sitz-/Preis-/Katalog-Wert), oder
// es gibt eine WEITERE Funktion (nicht FUN_00595a60), die beim [R]-Druck
// denselben Zone-Objekt-Fund-Mechanismus benutzt, aber DANACH tatsaechlich
// Items erzeugt/anzeigt.
//
// Dieser Abschnitt sucht darum gezielt nach Funktionen, die MEHRERE
// Offsets aus dem unkartierten Bereich (0xC4..0x188 in 4er-Schritten)
// zusammen referenzieren -- das sollte den echten Konsumenten dieser
// Daten zeigen (Kandidat fuer den Item-Generator).
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class VendorDisplayArrayResearch extends GhidraScript {

	// Bereits bekannte/verifizierte Adressen (siehe Kopfkommentar oben).
	private static final String ADDR_FUN_00434D10 = "00434d10"; // World::GetZone(x,y)
	private static final String ADDR_FUN_00595A60 = "00595a60"; // Ambient-Scan, findet Vendor-Gebaeude-Eintrag
	private static final String ADDR_FUN_005A4980 = "005a4980"; // Geschwisterfunktion
	private static final String ADDR_FUN_00459C40 = "00459c40"; // GameController-Konstruktor (baut u.a. 3 InventoryWidgets)
	private static final String ADDR_FUN_004889E0 = "004889e0"; // Interaktions-Dispatcher (char_class-Switch)
	private static final String ADDR_FUN_00488EE0 = "00488ee0"; // Session-/Pool-Cleanup + evtl. mehr (riesig, Timeout)

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_display_array_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorDisplayArrayResearch.java Ergebnisbericht (aufgeraeumte Fassung) ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			sectionA_MultiHitSearchInUnmappedZoneObjectTail();
			sectionB_CallersOfFun00595a60AndSibling();
			sectionC_CallersOfFun00434d10();
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Abschnitt A: programmweite Suche nach Funktionen, die MEHRERE (>= 3)
	// Offsets aus dem unkartierten zone_static_object-Bereich (0xC4..0x188,
	// 4er-Schritte) referenzieren. Wir wissen bereits, dass +0x178 (Index-
	// Vergleich) und +0x30 (Flag) relevant sind -- +0x30 liegt zwar noch im
	// kartierten Bereich, wird aber trotzdem mitgesucht, um Ueberschneidungen
	// mit bekannten Fundstellen (FUN_00595a60 etc.) zu erkennen und
	// auszusortieren.
	// -----------------------------------------------------------------
	private void sectionA_MultiHitSearchInUnmappedZoneObjectTail() throws Exception {
		writeReportLine("--- Abschnitt A: Funktionen mit >= 3 Treffern im unkartierten zone_static_object-Bereich (0xC4..0x188) ---");
		writeReportLine("  Ziel: der echte Item-Generator sollte mehrere Felder aus diesem Bereich");
		writeReportLine("  gemeinsam lesen (Position/Preis/Katalog-Referenz o.ae.), FUN_00595a60/");
		writeReportLine("  FUN_005a4980 (bereits als Sackgasse identifiziert) werden mitgelistet, aber");
		writeReportLine("  als BEKANNT markiert.");
		writeReportLine("");

		java.util.List<Long> targetOffsets = new ArrayList<>();
		for (long off = 0xC4L; off <= 0x188L; off += 4) {
			targetOffsets.add(off);
		}

		java.util.Map<Function, java.util.Set<Long>> perFunctionOffsets = new java.util.LinkedHashMap<>();
		java.util.Map<Function, List<String>> perFunctionLines = new java.util.LinkedHashMap<>();

		InstructionIterator instrs = currentProgram.getListing().getInstructions(true);
		while (instrs.hasNext()) {
			Instruction instr = instrs.next();
			for (int i = 0; i < instr.getNumOperands(); i++) {
				Object[] objs = instr.getOpObjects(i);
				for (Object o : objs) {
					if (o instanceof Scalar) {
						long val = ((Scalar) o).getUnsignedValue();
						if (targetOffsets.contains(val)) {
							Function fn = getFunctionContaining(instr.getAddress());
							if (fn == null) continue;
							perFunctionOffsets.computeIfAbsent(fn, k -> new java.util.TreeSet<>()).add(val);
							perFunctionLines.computeIfAbsent(fn, k -> new ArrayList<>()).add(
								"    Offset 0x" + Long.toHexString(val) + " @ " + instr.getAddress() + ":  " + instr.toString());
						}
					}
				}
			}
		}

		java.util.Set<String> knownDeadEnds = new java.util.HashSet<>(java.util.Arrays.asList(
			ADDR_FUN_00595A60, ADDR_FUN_005A4980));

		int reportedCount = 0;
		for (java.util.Map.Entry<Function, java.util.Set<Long>> entry : perFunctionOffsets.entrySet()) {
			if (entry.getValue().size() < 3) continue;
			Function fn = entry.getKey();
			boolean isKnown = knownDeadEnds.contains(fn.getEntryPoint().toString().toLowerCase());
			writeReportLine("  " + fn.getName() + " @ " + fn.getEntryPoint()
				+ "  (Treffer-Offsets: " + entry.getValue() + ")"
				+ (isKnown ? "   [BEKANNT -- bereits als Sackgasse identifiziert]" : "   [NEU]"));
			for (String line : perFunctionLines.get(fn)) {
				writeReportLine(line);
			}
			writeReportLine("");
			reportedCount++;
		}
		if (reportedCount == 0) {
			writeReportLine("  (keine Funktion referenziert 3 oder mehr Offsets aus diesem Bereich zusammen)");
			writeReportLine("");
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt B: Aufrufer (Xrefs) von FUN_00595a60 und FUN_005a4980 --
	// falls diese Ambient-Scan-Funktionen auch aus dem echten [R]-
	// Interaktionspfad heraus aufgerufen werden (nicht nur periodisch aus
	// der Ferne), waere das ein Hinweis, dass der Item-Generator direkt
	// DANACH im selben Aufrufer sitzt.
	// -----------------------------------------------------------------
	private void sectionB_CallersOfFun00595a60AndSibling() throws Exception {
		writeReportLine("--- Abschnitt B: Aufrufer von FUN_00595a60 / FUN_005a4980 ---");
		for (String addrStr : new String[] { ADDR_FUN_00595A60, ADDR_FUN_005A4980 }) {
			Address target = toAddr(addrStr);
			Function targetFn = getFunctionAt(target);
			writeReportLine(">> Aufrufer von " + (targetFn != null ? targetFn.getName() : addrStr) + " @ " + addrStr + ":");
			ReferenceManager refMgr = currentProgram.getReferenceManager();
			ReferenceIterator refs = refMgr.getReferencesTo(target);
			int count = 0;
			while (refs.hasNext()) {
				Reference ref = refs.next();
				Address from = ref.getFromAddress();
				Function fn = getFunctionContaining(from);
				String fnName = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
				writeReportLine("  Xref von " + from + " in " + fnName);
				count++;
			}
			if (count == 0) {
				writeReportLine("  (keine Xrefs gefunden)");
			}
			writeReportLine("");
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt C: Aufrufer (Xrefs) von FUN_00434d10 (World::GetZone) --
	// vollstaendige Liste, um ALLE Konsumenten von Zone-Daten zu sehen,
	// nicht nur die beiden bereits bekannten. Ein bisher uebersehener
	// Aufrufer koennte der echte Item-Generator sein.
	// -----------------------------------------------------------------
	private void sectionC_CallersOfFun00434d10() throws Exception {
		writeReportLine("--- Abschnitt C: ALLE Aufrufer von FUN_00434d10 (World::GetZone) ---");
		Address target = toAddr(ADDR_FUN_00434D10);
		ReferenceManager refMgr = currentProgram.getReferenceManager();
		ReferenceIterator refs = refMgr.getReferencesTo(target);
		int count = 0;
		java.util.Set<String> seen = new java.util.LinkedHashSet<>();
		while (refs.hasNext()) {
			Reference ref = refs.next();
			Address from = ref.getFromAddress();
			Function fn = getFunctionContaining(from);
			String fnName = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  Xref von " + from + " in " + fnName);
			if (fn != null) seen.add(fn.getEntryPoint().toString());
			count++;
		}
		if (count == 0) {
			writeReportLine("  (keine Xrefs gefunden)");
		}
		writeReportLine("");
		writeReportLine("  " + seen.size() + " unterschiedliche Aufrufer-Funktionen insgesamt.");
		writeReportLine("");
	}

	// -----------------------------------------------------------------
	// Hilfsfunktionen (unveraendert aus der Vorversion uebernommen).
	// -----------------------------------------------------------------
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
