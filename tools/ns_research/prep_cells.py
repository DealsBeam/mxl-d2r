"""Prepare a slot's installed MedianXL mod for the namespace experiment (reversible: backup dir nsresearch-backup)."""
import json, os, shutil, sys
slot = sys.argv[1]            # e.g. D:\D2R-93847-s3
mod = os.path.join(slot, "mods", "MedianXL")
data = os.path.join(mod, "MedianXL.mpq", "data")
bk = os.path.join(slot, "nsresearch-backup")
os.makedirs(bk, exist_ok=True)

def backup(path):
    rel = os.path.relpath(path, slot).replace(os.sep, "__")
    dst = os.path.join(bk, rel)
    if not os.path.exists(dst):
        shutil.copy2(path, dst)

SKIP = sys.argv[2].split(",") if len(sys.argv) > 2 else []

def edit(txt, rowkeycol, rowkey, changes, banks=("", "base")):
    if txt.replace(".txt", "") in SKIP:
        return
    for b in banks:
        p = os.path.join(data, "global", "excel", b, txt)
        if not os.path.exists(p):
            print("missing", p); continue
        backup(p)
        raw = open(p, "rb").read().decode("utf-8")
        eol = "\r\n" if "\r\n" in raw else "\n"
        lines = raw.split(eol)
        hdr = lines[0].split("\t")
        kc = hdr.index(rowkeycol)
        for i in range(1, len(lines)):
            cells = lines[i].split("\t")
            if len(cells) > kc and cells[kc] == rowkey:
                for col, val in changes.items():
                    ci = hdr.index(col)
                    while len(cells) <= ci: cells.append("")
                    cells[ci] = val
                lines[i] = "\t".join(cells)
                break
        else:
            print("row not found", txt, rowkey, b); continue
        open(p, "wb").write(eol.join(lines).encode("utf-8"))
        print("edited", txt, b or "excel", rowkey, list(changes))

ALL13 = ["enUS","zhTW","deDE","esES","frFR","itIT","koKR","plPL","esMX","jaJP","ptBR","ruRU","zhCN"]
recs = []
def rec(key, langs=None, text=None):
    r = {"Key": key}
    for l in (langs or ALL13):
        r[l] = (text or "NSTEST_" + key) + ("" if l == "enUS" else "_" + l)
    recs.append(r)

edit("misc.txt", "name", "Healing Potion", {"namestr": "medianxl:NsItem"}); rec("NsItem")
edit("armor.txt", "name", "crn", {"namestr": "medianxl:NsItemDe"}); rec("NsItemDe", ["deDE"])   # no enUS
edit("monstats.txt", "Id", "zombie1", {"NameStr": "medianxl:NsMon"}); rec("NsMon")
edit("levels.txt", "*StringName", "Rogue Encampment", {"LevelName": "medianxl:NsLvlName", "LevelWarp": "medianxl:NsLvlWarp", "LevelEntry": "medianxl:NsLvlEntry"})
rec("NsLvlName"); rec("NsLvlWarp"); rec("NsLvlEntry")
edit("levels.txt", "*StringName", "Blood Moor", {"LevelName": "medianxl:NsLvlBM"}); rec("NsLvlBM")
edit("skilldesc.txt", "skilldesc", "attack", {"str name": "medianxl:NsSkName", "str short": "medianxl:NsSkShort", "str long": "medianxl:NsSkLong", "str alt": "medianxl:NsSkAlt"})
for k in ("NsSkName", "NsSkShort", "NsSkLong", "NsSkAlt"): rec(k)
edit("itemstatcost.txt", "Stat", "strength", {"descstrpos": "medianxl:NsStatPos", "descstrneg": "medianxl:NsStatNeg", "descstr2": "medianxl:NsStat2", "dgrpstrpos": "medianxl:NsGrpPos"})
for k in ("NsStatPos", "NsStatNeg", "NsStat2", "NsGrpPos"): rec(k)
# the 3 strings the task asks for also live in median.json (above covers more)
sd = os.path.join(data, "local", "lng", "strings", "medianxl")
os.makedirs(sd, exist_ok=True)
json.dump(recs, open(os.path.join(sd, "median.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
mp = os.path.join(mod, "d2rloader", "metadata.json")
backup(mp)
m = json.load(open(mp, encoding="utf-8")); m["namespace"] = "medianxl"
json.dump(m, open(mp, "w", encoding="utf-8"), indent=2)
print("records", len(recs))
