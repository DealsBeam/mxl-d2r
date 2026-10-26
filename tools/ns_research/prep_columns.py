"""Column survey: valid medianxl keys in row 1 and a nonexistent key (medianxl:NoSuchKey) in row 2 of each column."""
import json, os, shutil, sys
slot = sys.argv[1]
mod = os.path.join(slot, "mods", "D2R-XL"); data = os.path.join(mod, "D2R-XL.mpq", "data")
bk = os.path.join(slot, "nsresearch-backup"); os.makedirs(bk, exist_ok=True)
def backup(path):
    dst = os.path.join(bk, os.path.relpath(path, slot).replace(os.sep, "__"))
    if not os.path.exists(dst): shutil.copy2(path, dst)
recs = []
ALL13 = ["enUS","zhTW","deDE","esES","frFR","itIT","koKR","plPL","esMX","jaJP","ptBR","ruRU","zhCN"]
def rec(key):
    recs.append(dict({"Key": key}, **{l: "NSTEST_" + key for l in ALL13}))
def edit(txt, col, valid, bogus="medianxl:NoSuchKey", banks=("", "base")):
    for b in banks:
        p = os.path.join(data, "global", "excel", b, txt)
        if not os.path.exists(p): continue
        backup(p)
        raw = open(p, "rb").read().decode("utf-8"); eol = "\r\n" if "\r\n" in raw else "\n"
        lines = raw.split(eol); hdr = lines[0].split("\t"); ci = hdr.index(col)
        n = 0
        for i in range(1, len(lines)):
            cells = lines[i].split("\t")
            if len(cells) <= ci or cells[0] == "" : continue
            n += 1
            if n in (1, 2):
                cells[ci] = valid if n == 1 else bogus
                lines[i] = "\t".join(cells)
            if n == 2: break
        open(p, "wb").write(eol.join(lines).encode("utf-8"))
    if valid.startswith("medianxl:"): rec(valid.split(":", 1)[1])
    print("edited", txt, col)
for txt, col, k in [("uniqueitems.txt", "index", "NsUnique"), ("setitems.txt", "index", "NsSetItem"), ("sets.txt", "name", "NsSet"),
                    ("magicprefix.txt", "Name", "NsPre"), ("magicsuffix.txt", "Name", "NsSuf"), ("rareprefix.txt", "name", "NsRarePre"),
                    ("raresuffix.txt", "name", "NsRareSuf"), ("runes.txt", "Name", "NsRune"), ("superuniques.txt", "Name", "NsSuper"),
                    ("misc.txt", "namestr", "NsMisc"), ("weapons.txt", "namestr", "NsWeap"), ("monstats.txt", "DescStr", "NsMonDesc"),
                    ("misc.txt", "spelldescstr", "NsSpellDesc"), ("lowqualityitems.txt", "Name", "NsLowQ")]:
    if os.environ.get("NS_ONLY") and os.environ["NS_ONLY"] != txt + ":" + col: continue
    try: edit(txt, col, "medianxl:" + k)
    except Exception as e: print("skip", txt, col, e)
# a key too long for a 32-byte field
if os.environ.get("NS_ONLY") in (None, "long"):
  try: edit("uniqueitems.txt", "index", "medianxl:" + "NsLongKey" * 3, bogus="medianxl:" + "NsLongKey" * 3); rec("NsLongKey" * 3)
  except Exception as e: print("skip long", e)
sd = os.path.join(data, "local", "lng", "strings", "medianxl"); os.makedirs(sd, exist_ok=True)
json.dump(recs, open(os.path.join(sd, "median.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
mp = os.path.join(mod, "d2rloader", "metadata.json"); backup(mp)
m = json.load(open(mp, encoding="utf-8")); m["namespace"] = "medianxl"; json.dump(m, open(mp, "w", encoding="utf-8"), indent=2)
print("records", len(recs))
