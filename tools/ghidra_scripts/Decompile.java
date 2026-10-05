// Decompile functions (by RVA or address) into ../ghidra/decomp/<rva>.c and print where they went.
// Args: rva [rva...]   (hex, e.g. 0x3dd710; values >= the image base are taken as addresses)
//@category mxl
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.FileWriter;

public class Decompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        File outDir = new File(currentProgram.getExecutablePath()).getParentFile();
        outDir = new File(outDir, "decomp");
        outDir.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String arg : getScriptArgs()) {
            long v = Long.decode(arg);
            Address a = toAddr(v >= base ? v : base + v);
            Function f = getFunctionContaining(a);
            if (f == null) { println("no function at " + a); continue; }
            DecompileResults res = di.decompileFunction(f, 120, monitor);
            String rva = String.format("0x%x", f.getEntryPoint().getOffset() - base);
            File out = new File(outDir, rva + ".c");
            try (FileWriter w = new FileWriter(out)) {
                w.write("// " + f.getName() + " rva " + rva + "\n");
                w.write(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// failed: " + res.getErrorMessage());
            }
            println("DECOMP " + rva + " " + f.getName() + " -> " + out.getPath());
        }
        di.dispose();
    }
}
