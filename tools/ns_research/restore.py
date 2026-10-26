import os, shutil, sys
slot = sys.argv[1]; bk = os.path.join(slot, "nsresearch-backup")
for f in os.listdir(bk):
    if f.endswith(".orig"): continue   # plugin dll: restored separately
    rel = f.replace("__", os.sep)
    shutil.copy2(os.path.join(bk, f), os.path.join(slot, rel)); print("restored", rel)
d = os.path.join(slot, "mods", "D2R-XL", "D2R-XL.mpq", "data", "local", "lng", "strings", "medianxl")
if os.path.isdir(d): shutil.rmtree(d); print("removed", d)
