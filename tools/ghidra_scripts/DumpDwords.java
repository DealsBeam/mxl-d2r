// Dump little-endian dwords from program memory. Args: rva count [rva count ...]   Output: "DW <rva>: v v v v"
//@category mxl
import ghidra.app.script.GhidraScript;

public class DumpDwords extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        String[] a = getScriptArgs();
        for (int k = 0; k + 1 < a.length; k += 2) {
            long rva = Long.decode(a[k]);
            int n = Integer.decode(a[k + 1]);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < n; i++) {
                if (i % 4 == 0) {
                    if (i > 0) { println(sb.toString()); sb.setLength(0); }
                    sb.append(String.format("DW 0x%x:", rva + 4L * i));
                }
                int v = getInt(toAddr(base + rva + 4L * i));
                sb.append(String.format(" %08x", v));
            }
            println(sb.toString());
        }
    }
}
