// VendorInitCallerResearch.java
// ---------------------------------------------------------------------
// Direkte Fortsetzung von CreatureCatalogFieldResearch.java.
//
// Dort gefunden: FUN_004772b0 @ 0x004772b0 ist mutmasslich die native
// "Vendor-Katalog initialisieren"-Funktion -- sie baut den Kreatur+0x11dc-
// STDVector aus einer per Namens-Lookup gefundenen Vorlage auf, vergroessert
// die ersten 4 Kategorien auf min. 50 Item-Slots und fuellt ueber
// FUN_0046ebe0() je nach Kreatur+0x140-Typ-Byte (Faelle 1-4) UI-Tab-
// Eintraege.
//
// ZIEL DIESES SKRIPTS (zwei Fragen aus dem Chat):
//  A) Wer ruft FUN_004772b0 auf, und unter welcher Bedingung? -> direkte
//     Xref-Suche auf CALL-Referenzen zur Funktionsadresse.
//  B) Was bestimmt den Namens-String, den FUN_0043c020 fuer den Vorlagen-
//     Lookup benutzt (wird nach Kreatur+0x300 geschrieben, siehe Zeile
//     "FUN_0044af00(...+0x300, puVar3)" im Decompile von FUN_004772b0)?
//     Dafuer: Aufrufer von FUN_0043bc00 und FUN_0044af00 mit ausgeben,
//     sowie den Decompile von FUN_0043bc00/FUN_0043c020 selbst, um zu
//     sehen, woher der Name kommt (Kreatur-Feld? Tabelle nach Typ-Byte?).
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class VendorInitCallerResearch extends GhidraScript {

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_init_caller_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorInitCallerResearch.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			// Frage A: wer ruft FUN_004772b0 auf?
			sectionA_CallersOf("FUN_004772b0 (mutmassliche Vendor-Katalog-Init)", "004772b0");

			// Frage B: woher kommt der Namens-String fuer den Vorlagen-Lookup?
			// Erst die beiden Helferfunktionen selbst decompilieren...
			decompileAndReport("FUN_0043bc00 (baut puVar3, siehe Aufruf in FUN_004772b0)", "0043bc00", 60);
			decompileAndReport("FUN_0044af00 (schreibt Name nach Kreatur+0x300)", "0044af00", 60);
			decompileAndReport("FUN_0043c020 (Vorlagen-Lookup anhand des Namens)", "0043c020", 60);
			// ...dann deren Aufrufer, um zu sehen, in welchen Kontexten das
			// Namens-Lookup-Muster sonst noch verwendet wird.
			sectionA_CallersOf("FUN_0043bc00 (Namens-String-Aufbau)", "0043bc00");
			sectionA_CallersOf("FUN_0043c020 (Vorlagen-Lookup)", "0043c020");
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Findet alle direkten CALL-Xrefs auf die Funktion an addrStr, listet
	// die aufrufende Funktion + Adresse, und decompiliert jede aufrufende
	// Funktion einmal (dedupliziert).
	// -----------------------------------------------------------------
	private void sectionA_CallersOf(String label, String addrStr) throws Exception {
		writeReportLine("--- Aufrufer von " + label + " @ " + addrStr + " ---");
		Address targetAddr = toAddr(addrStr);
		Function targetFn = getFunctionAt(targetAddr);
		if (targetFn == null) {
			writeReportLine("   (keine Funktion an dieser Adresse -- Imagebase pruefen!)");
			writeReportLine("");
			return;
		}

		List<Address> callerFuncStarts = new ArrayList<>();
		ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(targetAddr);
		int count = 0;
		while (refs.hasNext()) {
			Reference ref = refs.next();
			if (!ref.getReferenceType().isCall()) continue;
			count++;
			Address from = ref.getFromAddress();
			Function fn = getFunctionContaining(from);
			String fnLabel = (fn != null) ? fn.getName() + " @ " + fn.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  Call von " + from + " in " + fnLabel);
			if (fn != null && !callerFuncStarts.contains(fn.getEntryPoint())) {
				callerFuncStarts.add(fn.getEntryPoint());
			}
		}
		writeReportLine("  " + count + " Call-Xrefs, " + callerFuncStarts.size() + " unterschiedliche Aufrufer-Funktionen.");
		writeReportLine("");

		for (Address fnAddr : callerFuncStarts) {
			decompileAndReport("Aufrufer-Funktion von " + label, fnAddr.toString(), 60);
		}
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
