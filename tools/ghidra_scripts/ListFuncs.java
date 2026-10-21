// List functions whose entry lies in [lo, hi) (RVAs). Args: lo hi   Output: "FUNC <rva> size 0x.."
//@category mxl
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ListFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        String[] a = getScriptArgs();
        long lo = Long.decode(a[0]), hi = Long.decode(a[1]);
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(base + lo), true);
        while (it.hasNext()) {
            Function f = it.next();
            long rva = f.getEntryPoint().getOffset() - base;
            if (rva >= hi) break;
            println(String.format("FUNC 0x%x size 0x%x", rva, f.getBody().getNumAddresses()));
        }
    }
}
