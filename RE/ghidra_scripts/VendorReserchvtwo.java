// VendorReserchvtwo.java
// ---------------------------------------------------------------------
// Nachfolger von VendorDisplayArrayResearch.java (siehe dort fuer den
// vollen Stand der Recherche bis Abschnitt Z / neue Abschnitt A-C).
// Neuer Dateiname auf Nutzerwunsch. WICHTIG: Datei- und Klassenname
// muessen fuer Ghidra exakt uebereinstimmen ("VendorReserchvtwo").
//
// ==== STAND (siehe VendorDisplayArrayResearch.java fuer Details) ====
// - creature+0x1b0/+0x1b4/+0x1b8 = ZonenX/ZonenY/Gebaeude-Index.
// - FUN_00434d10 = World::GetZone(x,y).
// - FUN_00595a60/FUN_005a4980 = Ambient-Scan, findet Vendor-Gebaeude
//   (type==5) in der Zone, aber NUR Benachrichtigungs-Code danach --
//   SACKGASSE fuer die Item-Generierung.
// - NEUER FUND (Abschnitt C der Vorversion): FUN_00488ee0 (die grosse,
//   bereits bekannte Session-/Pool-Cleanup-Funktion) ruft FUN_00434d10
//   an FUENF verschiedenen Stellen auf: 0x48d66e, 0x48f4ab, 0x48f610,
//   0x4995ed, 0x49d1ad. Das wurde bisher nie in diesem Zusammenhang
//   untersucht -- starker Kandidat fuer den echten Item-Generator.
//
// ZIEL DIESES SKRIPTS: Disassembly-Fenster um alle 5 GetZone-Aufrufstellen
// in FUN_00488ee0, um zu sehen, WOFUER dort wiederholt Zonen abgefragt
// werden (evtl. "gehe alle Gebaeude/Slots durch und generiere Items").
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.util.task.TaskMonitor;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class VendorReserchvtwo extends GhidraScript {

	private static final String ADDR_FUN_00488EE0 = "00488ee0";
	private static final String ADDR_FUN_004C1BB0 = "004c1bb0"; // cube::InventoryWidget Konstruktor

	private static final String[] GETZONE_CALLSITES_IN_FUN_00488EE0 = {
		"0048d66e", "0048f4ab", "0048f610", "004995ed", "0049d1ad"
	};

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "vendor_reserchvtwo_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== VendorReserchvtwo.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			sectionA_WindowsAroundGetZoneCallsInFun00488ee0();
			sectionB_CallersOfInventoryWidgetConstructor();
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Abschnitt A: grosszuegige Disassembly-Fenster (40 davor, 60 danach)
	// um jede der 5 bekannten FUN_00434d10-Aufrufstellen in FUN_00488ee0.
	// FUN_00488ee0 selbst ist zu gross fuer volles Decompile (Timeout),
	// darum hier gezielte Fenster statt Gesamt-Decompile.
	// -----------------------------------------------------------------
	private void sectionA_WindowsAroundGetZoneCallsInFun00488ee0() throws Exception {
		writeReportLine("--- Abschnitt A: Fenster um die 5 GetZone-Aufrufstellen in FUN_00488ee0 ---");
		writeReportLine("  Ziel: wofuer werden hier wiederholt Zonen abgefragt? Kandidat fuer den");
		writeReportLine("  echten Icon-Fenster-Item-Generator.");
		writeReportLine("");
		for (String addrStr : GETZONE_CALLSITES_IN_FUN_00488EE0) {
			writeReportLine(">> Fenster um " + addrStr + " (40 davor, 60 danach):");
			printInstructionWindow(toAddr(addrStr), 40, 60);
			writeReportLine("");
		}
	}

	// -----------------------------------------------------------------
	// Abschnitt B: ALLE Aufrufer von FUN_004c1bb0 (cube::InventoryWidget-
	// Konstruktor) finden und jeweils die Aufruf-Umgebung decompilieren.
	// Bisher (Abschnitt S der Vorversion) waren nur 3 Aufrufstellen bekannt
	// (waehrend GameController-Aufbau vermutet). Ziel hier: bestaetigen,
	// ob es WIRKLICH nur diese 3 sind, und pruefen, ob einer der Aufrufer
	// mit dem Talk-/Shop-Fenster (statt statischen Kategorie-Tabs) zu tun
	// hat -- z.B. durch String-/Funktionsnamen-Kontext im Decompile.
	// -----------------------------------------------------------------
	private void sectionB_CallersOfInventoryWidgetConstructor() throws Exception {
		writeReportLine("--- Abschnitt B: Aufrufer von FUN_004c1bb0 (InventoryWidget-Konstruktor) ---");
		Address target = toAddr(ADDR_FUN_004C1BB0);
		ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
		List<Address> callerFuncStarts = new ArrayList<>();
		int count = 0;
		while (refs.hasNext()) {
			Reference ref = refs.next();
			Address from = ref.getFromAddress();
			Function callerFn = getFunctionContaining(from);
			String fnLabel = (callerFn != null) ? callerFn.getName() + " @ " + callerFn.getEntryPoint() : "(keine Funktion)";
			writeReportLine("  Xref von " + from + " in " + fnLabel);
			count++;
			if (callerFn != null && !callerFuncStarts.contains(callerFn.getEntryPoint())) {
				callerFuncStarts.add(callerFn.getEntryPoint());
			}
		}
		writeReportLine("  " + count + " Xrefs in " + callerFuncStarts.size() + " unterschiedlichen Funktionen.");
		writeReportLine("");

		for (Address fnAddr : callerFuncStarts) {
			decompileAndReport("Aufrufer-Funktion von InventoryWidget-Ctor", fnAddr.toString(), 60);
		}
	}

	// -----------------------------------------------------------------
	// Hilfsfunktionen.
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
