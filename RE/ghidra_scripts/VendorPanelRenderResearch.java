// VendorPanelRenderResearch.java
// ---------------------------------------------------------------------
// Direkte Fortsetzung von CreatureCatalogFieldResearch.java /
// VendorInitCallerResearch.java.
//
// STAND (siehe Chat-Historie): Wir bauen den Vendor-Katalog direkt in
// GC+0x800c0c (== InventoryWidget3+0x160) mit eigenen Item-Structs.
// Zwei Varianten getestet:
//   1) Nur category_id/item_id uebernommen, Rest genullt -> Items im
//      "VENDOR"-Panel erscheinen als leere Platzhalter (Icon da, aber
//      kein Name/keine sinnvollen Werte).
//   2) Komplette 1:1-Struct-Kopie der getragenen Ausruestung (inkl.
//      customization_data/adaption_count) -> GLEICHES Ergebnis, immer
//      noch teils leere/falsch benannte Eintraege.
//   3) Versuch, Items ueber die native Template-Lookup-Funktion
//      FUN_00595010 zu bauen (dieselbe, die im SEPARATEN Sprechblasen-
//      Handelsfenster/"wares" funktioniert) -> stuerzt IMMER ab
//      (zuletzt bei statischer Adresse 0x4da1d3), auch mit dem
//      PrimeWorldTileCache-Fix (FUN_00487da0 vorher aufgerufen).
//
// OFFENE FRAGE: Rendert FUN_004c2050 (die gemeinsame Render-Funktion
// von InventoryWidget1/2/3 UND CharacterWidget, siehe Kommentar bei
// DumpInventoryWidgetsDebug in main.cpp) den Namen/Preis/Icon einer
// Zeile DIREKT aus dem Item-Struct (category_id/item_id/rarity/...),
// oder ueber eine SEPARATE Zwischenschicht -- aehnlich der bereits
// gefundenen "Display-Cache-Zeilen" (session+0x178, siehe
// BuildDisplayCacheLines in WanderingMerchant.cpp), die für das
// Sprechblasen-Fenster existiert, aber für DIESES Panel (GC+0x80095C)
// noch nicht gefunden wurde?
//
// ZIEL DIESES SKRIPTS:
//   A) FUN_004c2050 selbst decompilieren -- das ist der zentrale
//      Kandidat, in dem (je nach +0x194-Modus) die Zeilen des Panels
//      aufgebaut werden.
//   B) Alle Funktionen finden/decompilieren, die FUN_004c2050 fuer den
//      MODUS==3 (Vendor)-Zweig aufruft (Name-/Preis-/Icon-Berechnung),
//      indem wir nach Aufrufen in der Naehe von Vergleichen mit dem
//      Wert 3 (fuer +0x194) suchen -- als Textsuche im Decompile.
//   C) Zusaetzlich: alle Xrefs auf +0x1b4 (Sub-Index-Feld) und +0x198,
//      um zu sehen, ob dort noch weitere Render-Helfer haengen.
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

public class VendorPanelRenderResearch extends GhidraScript {

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_panel_render_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorPanelRenderResearch.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			// A) Die zentrale Render-Funktion selbst.
			decompileAndReport("FUN_004c2050 (Render-Funktion InventoryWidget1/2/3 + CharacterWidget)", "004c2050", 90);

			// B) Aufrufer von FUN_004c2050 (falls es selbst nur ein Helfer ist).
			sectionCallersOf("FUN_004c2050 (Render-Funktion)", "004c2050");

			// C) Konstruktor, zum Vergleich (kennen wir schon, aber fuer
			// vollstaendigen Kontext im Report mit ausgeben).
			decompileAndReport("FUN_004c1bb0 (Konstruktor InventoryWidget*/CharacterWidget)", "004c1bb0", 60);
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	private void sectionCallersOf(String label, String addrStr) throws Exception {
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
