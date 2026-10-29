// VendorPanelResearch.java
// ---------------------------------------------------------------------
// NEUER Rechercheschwerpunkt (siehe Video-Analyse: WanderingMerchant.cpp/
// Chat-Historie). Bisherige Skripte (VendorDisplayArrayResearch.java,
// VendorReserchvtwo.java) haben die Datenquelle der NATIVEN SPRECHBLASE
// gefunden (wares+0x160 / session+0x178, siehe BuildDisplayCacheLines() in
// WanderingMerchant.cpp) -- das ist aber NICHT das Kaufmenue.
//
// Per Video zweifelsfrei bestaetigt: das eigentliche Kaufmenue ist ein
// EIGENES Panel mit der Ueberschrift "VENDOR" (rechts neben "EQUIPMENT"/
// "MY OWN", oberer Tab = Rucksack-Symbol). Es zeigt eine scrollbare Liste:
// Icon + Name + Preis pro Zeile (z.B. "GLASS FLASK" / "2 C", gruen
// hervorgehobene "FORMULA: ..."-Eintraege fuer Rezepte). Bei unserem
// eigenen Haendler bleibt dieses Panel LEER, obwohl wares(+0x160) gefuellt
// ist -- also eine ANDERE Datenquelle/Klasse als bisher angenommen.
//
// cube::InventoryWidget (FUN_004c1bb0-Konstruktor) wurde bereits exhaustiv
// per Xref-Suche auf GENAU 3 Aufrufstellen zurueckverfolgt (alle im
// GameController-Konstruktor FUN_00459c40, alle fuer die 3 Spieler-
// Inventar-Tabs Equipment/Items/Ingredients) -- kann also NICHT die Klasse
// hinter dem "VENDOR"-Panel sein.
//
// ZIEL DIESES SKRIPTS: ueber die sichtbaren UI-Strings ("VENDOR",
// "FORMULA:") und deren Xrefs die tatsaechliche Populierungs-Funktion
// finden.
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class VendorPanelResearch extends GhidraScript {

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_panel_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorPanelResearch.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			sectionA_FindStringAndXrefs("VENDOR");
			sectionA_FindStringAndXrefs("FORMULA:");
			sectionB_WritersToOffset0x800954And95C();
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Abschnitt A: sucht alle definierten String-Daten, deren Wert die
	// gesuchte Zeichenkette ENTHAELT (case-insensitive), listet deren
	// Adresse + alle Xrefs (mit umschliessender Funktion) und decompiliert
	// jede dieser Aufrufer-Funktionen (Timeout 60s, bei Fehlschlag wird das
	// vermerkt statt das Skript abzubrechen).
	// -----------------------------------------------------------------
	private void sectionA_FindStringAndXrefs(String needle) throws Exception {
		writeReportLine("--- Suche nach String, der \"" + needle + "\" enthaelt ---");
		String needleLower = needle.toLowerCase();
		List<Address> matchAddrs = new ArrayList<>();

		DataIterator dataIt = currentProgram.getListing().getDefinedData(currentProgram.getMinAddress(), true);
		while (dataIt.hasNext()) {
			Data data = dataIt.next();
			if (data == null || !data.hasStringValue()) continue;
			Object value = data.getValue();
			if (value == null) continue;
			String s = value.toString();
			if (s.toLowerCase().contains(needleLower)) {
				matchAddrs.add(data.getAddress());
			}
		}

		writeReportLine("  " + matchAddrs.size() + " Treffer.");
		List<Address> callerFuncStarts = new ArrayList<>();

		for (Address strAddr : matchAddrs) {
			Data d = getDataAt(strAddr);
			String val = (d != null && d.getValue() != null) ? d.getValue().toString() : "?";
			writeReportLine("  String @ " + strAddr + ": \"" + val + "\"");

			ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(strAddr);
			while (refs.hasNext()) {
				Reference ref = refs.next();
				Address from = ref.getFromAddress();
				Function fn = getFunctionContaining(from);
				String fnLabel = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
				writeReportLine("    Xref von " + from + " in " + fnLabel);
				if (fn != null && !callerFuncStarts.contains(fn.getEntryPoint())) {
					callerFuncStarts.add(fn.getEntryPoint());
				}
			}
		}
		writeReportLine("");

		for (Address fnAddr : callerFuncStarts) {
			decompileAndReport("Aufrufer-Funktion (String \"" + needle + "\")", fnAddr.toString(), 60);
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt B: NEUER Fund (Live-Test per [U]-Widget-Dump, siehe Chat):
	// GC->InventoryWidget3 (Offset 0x80095C relativ zu GameController) ist
	// PERMANENT im Modus 3 ("Vendor") -- keine umgeschaltete Instanz,
	// sondern von Anfang an die Vendor-Widget-Instanz. Gesucht wird jetzt,
	// wer diesen Zeiger dereferenziert (Scalar 0x80095c als Speicher-
	// Verschiebung) UND dabei auch +0x160 beschreibt -- das waere die
	// gesuchte "Befuelle die Vendor-Liste"-Funktion. Zum Vergleich auch
	// 0x800954 (IW1) und 0x800958 (IW2), um den Unterschied zu sehen.
	// -----------------------------------------------------------------
	private void sectionB_WritersToOffset0x800954And95C() throws Exception {
		writeReportLine("--- Abschnitt B: Aufrufer, die GC+0x800954/0x800958/0x80095C referenzieren ---");
		long[] targets = { 0x800954L, 0x800958L, 0x80095cL };
		String[] labels = { "InventoryWidget1 (0x800954)", "InventoryWidget2 (0x800958)", "InventoryWidget3 (0x80095C, VENDOR)" };

		List<Address> allCallerFuncs = new ArrayList<>();
		for (int t = 0; t < targets.length; t++) {
			writeReportLine(">> Suche Scalar 0x" + Long.toHexString(targets[t]) + " (" + labels[t] + "):");
			List<Address> hits = findScalarOperandHits(targets[t]);
			writeReportLine("   " + hits.size() + " Instruktions-Treffer.");
			for (Address hitAddr : hits) {
				Function fn = getFunctionContaining(hitAddr);
				String fnLabel = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
				writeReportLine("     " + hitAddr + " in " + fnLabel);
				if (fn != null && !allCallerFuncs.contains(fn.getEntryPoint())) {
					allCallerFuncs.add(fn.getEntryPoint());
				}
			}
		}
		writeReportLine("  " + allCallerFuncs.size() + " unterschiedliche Funktionen insgesamt.");
		writeReportLine("");

		for (Address fnAddr : allCallerFuncs) {
			decompileAndReport("Aufrufer-Funktion (referenziert 0x800954/58/5C)", fnAddr.toString(), 60);
		}
	}

	// Durchsucht alle Instruktionen im Programm nach einem Scalar-Operanden
	// mit exakt diesem Wert (z.B. als Speicher-Verschiebung "[EAX+0x80095c]").
	private List<Address> findScalarOperandHits(long value) {
		List<Address> results = new ArrayList<>();
		ghidra.program.model.listing.InstructionIterator instrs =
			currentProgram.getListing().getInstructions(currentProgram.getMinAddress(), true);
		while (instrs.hasNext()) {
			ghidra.program.model.listing.Instruction instr = instrs.next();
			int numOps = instr.getNumOperands();
			for (int i = 0; i < numOps; i++) {
				Object[] opObjs = instr.getOpObjects(i);
				for (Object o : opObjs) {
					if (o instanceof ghidra.program.model.scalar.Scalar) {
						ghidra.program.model.scalar.Scalar s = (ghidra.program.model.scalar.Scalar) o;
						if (s.getUnsignedValue() == value) {
							results.add(instr.getAddress());
						}
					}
				}
			}
		}
		return results;
	}

	// -----------------------------------------------------------------
	// Hilfsfunktionen (identisches Muster wie in den Vorgaengerskripten).
	// -----------------------------------------------------------------
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
