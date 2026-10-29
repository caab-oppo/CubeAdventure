// CreatureCatalogFieldResearch.java
// ---------------------------------------------------------------------
// Direkte Fortsetzung von VendorPanelResearch.java Abschnitt B.
//
// Live-Befund (per [U]-Debug-Dump, siehe Chat-Historie): Kreatur+0x11dc/
// +0x11e0/+0x11e4 ist ein STDVector-Header, der laut FUN_004a2300-Decompile
// die QUELLE ist, aus der GC+0x800c0c (und darueber das native "VENDOR"-
// Panel) befuellt wird (FUN_0044ad30(gc+0x800c0c, kreatur+0x11dc)).
//
// Beobachtung: bei manchen Spawns unseres eigenen Haendlers ist dieses Feld
// gueltig befuellt (Panel zeigt echte Items), bei anderen Spawns komplett
// Null (Panel bleibt leer) -- unser SpawnCreature()-Modcode schreibt dieses
// Feld NICHT selbst, es ist offenbar Speicher-Restmuell.
//
// WICHTIG (Nutzervorgabe): Ziel ist NICHT, die native Init-Funktion
// nachzuahmen/aufzurufen, sondern zu verstehen, WELCHES Datenformat dort
// erwartet wird (insb. die itemArrayStart/End-Zeiger je Kategorie-Eintrag),
// damit wir SELBST einen gueltigen Katalog-Vektor mit UNSEREN EIGENEN Items
// bauen und in kreatur+0x11dc einhaengen koennen -- unabhaengig davon, ob
// die Kreatur beim Spawn zufaellig gueltigen Restmuell "erbt" oder nicht.
//
// ZIEL DIESES SKRIPTS: alle Funktionen finden, die den Scalar-Offset
// 0x11dc (und die Nachbarfelder 0x11e0/0x11e4/0x11e8/0x11ec/0x1304/0x1308,
// die wir ebenfalls im [U]-Dump mitloggen) als Speicher-Verschiebung
// benutzen -- das sind Kandidaten fuer die native
// "baue/initialisiere Vendor-Katalog"-Funktion bzw. Leser/Schreiber dieses
// Feldes, aus deren Decompile wir das exakte Datenformat ablesen koennen.
// ---------------------------------------------------------------------

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class CreatureCatalogFieldResearch extends GhidraScript {

	private DecompInterface decompiler;
	private BufferedWriter report;

	@Override
	protected void run() throws Exception {
		File reportFile = new File(getSourceFile().getFile(false).getParentFile(), "creature_catalog_field_research_output.txt");
		report = new BufferedWriter(new FileWriter(reportFile, false));

		decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);

		writeReportLine("=== CreatureCatalogFieldResearch.java Ergebnisbericht ===");
		writeReportLine("Programm: " + currentProgram.getName());
		writeReportLine("Imagebase: " + currentProgram.getImageBase());
		writeReportLine("");

		try {
			sectionA_WritersToCreatureOffsets();
		} finally {
			decompiler.dispose();
			report.flush();
			report.close();
		}

		println("Fertig. Bericht geschrieben nach: " + reportFile.getAbsolutePath());
	}

	// -----------------------------------------------------------------
	// Sucht alle Instruktionen im Programm, die einen der Ziel-Offsets als
	// Scalar-Operanden (Speicher-Verschiebung, z.B. "[ECX+0x11dc]") nutzen.
	// Fuer jede Treffer-Funktion wird einmal decompiliert (dedupliziert).
	// -----------------------------------------------------------------
	private void sectionA_WritersToCreatureOffsets() throws Exception {
		writeReportLine("--- Aufrufer, die Kreatur+0x11dc und Nachbarfelder referenzieren ---");
		long[] targets = { 0x11dcL, 0x11e0L, 0x11e4L, 0x11e8L, 0x11ecL, 0x1304L, 0x1308L };
		String[] labels = {
			"+0x11dc (Vorlage Start?)",
			"+0x11e0 (evtl. End)",
			"+0x11e4 (evtl. Cap)",
			"+0x11e8",
			"+0x11ec",
			"+0x1304",
			"+0x1308"
		};

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
			decompileAndReport("Aufrufer-Funktion (referenziert 0x11dc/0x11e0/0x11e4/0x11e8/0x11ec/0x1304/0x1308)", fnAddr.toString(), 60);
		}
	}

	// Durchsucht alle Instruktionen im Programm nach einem Scalar-Operanden
	// mit exakt diesem Wert (z.B. als Speicher-Verschiebung "[EAX+0x11dc]").
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
	// Hilfsfunktionen (identisches Muster wie in VendorPanelResearch.java).
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
