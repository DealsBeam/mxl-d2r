// Disassembly of the functions at the given RVAs (or addresses), one instruction per line.
// Args: rva [rva...]   Output: "ASM <rva>: <instruction>"
//@category mxl
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class Disasm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        for (String arg : getScriptArgs()) {
            long v = Long.decode(arg);
            Address a = toAddr(v >= base ? v : base + v);
            Function f = getFunctionContaining(a);
            if (f == null) { println("no function at " + a); continue; }
            println(String.format("FUNC %s@0x%x", f.getName(), f.getEntryPoint().getOffset() - base));
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                println(String.format("ASM 0x%x: %s", ins.getAddress().getOffset() - base, ins));
            }
        }
    }
}
