// Decompiles the functions containing the given module offsets (as printed by the loader, e.g.
// "webkit_wkc+0x14e93b4"). For use with analyzeHeadless -process on an nro2elf.py ELF, which
// Ghidra loads at image base 0x100000.
//
//   analyzeHeadless <projdir> <project> -process webkit_wkc.elf -noanalysis -readOnly \
//     -scriptPath scripts/ghidra -postScript DecompileAt.java 0x14e93b4 0x14bf7ec ...
//
// Each argument is treated as a return address (the call site is the instruction before it).
// A leading "--all" argument prints whole functions instead of the first 80 lines.
// @category NXB

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    private static final long IMAGE_BASE = 0x100000L;
    private static final int MAX_LINES = 80;

    @Override
    protected void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        int maxLines = MAX_LINES;
        for (String arg : getScriptArgs()) {
            if (arg.equals("--all")) {
                maxLines = Integer.MAX_VALUE;
                continue;
            }
            long off = Long.decode(arg);
            Address callSite = toAddr(IMAGE_BASE + off - 4);
            Function f = getFunctionContaining(callSite);
            if (f == null) {
                println("=== +" + arg + ": no function");
                continue;
            }
            long entryOff = f.getEntryPoint().getOffset() - IMAGE_BASE;
            println(String.format("=== +%s in %s (entry +0x%x, call site +0x%x)", arg, f.getName(), entryOff, off - 4));
            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            if (res.getDecompiledFunction() == null) {
                println("  (decompile failed: " + res.getErrorMessage() + ")");
                continue;
            }
            String[] lines = res.getDecompiledFunction().getC().split("\n");
            for (int i = 0; i < Math.min(lines.length, maxLines); i++) println("  " + lines[i]);
            if (lines.length > maxLines) println("  ... (" + (lines.length - maxLines) + " more lines)");
        }
        decomp.dispose();
    }
}
