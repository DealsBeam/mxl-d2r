# Vendors

Median leaves every item's vendor columns (`<Npc>Min`, `Max`, `MagicMin`, `MagicMax`, `MagicLvl` in
weapons/armor/misc) empty. D2Sigma stocks shops from its own table, `npcstores.bin` (847 rows of 52 bytes).
`txt_migrate.py` (`vendor_stock`) converts it to D2R's per-item columns.

## npcstores.bin layout

Read from the data (the field names are not in D2Sigma; its compiler builds them at run time):

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0 | u16 | NPC, monstats row (147 Gheed, 148 Akara, 254 Alkor, 512 Drehya/Anya, ...) | all 16 values are town NPCs |
| +4 | char[4] | item code, space padded | every code is a weapons/armor/misc code |
| +8 | u8 | always in stock | 1 only on consumables: potions, keys, mystic orbs, oils, Cain's `ktpx` |
| +16 | u8 | Min (normal copies) | 0-1 |
| +17 | u8 | Max | 0-2 |
| +19 | u8 | 25 on 297 equipment rows, 0 elsewhere. Not a MagicLvl: it is Median's constant of the normal-copy limit (normal copies only while the item level is below 25) | the builder D2Sigma 0x93a30 compares the item level with 25; writing it into D2R's `MagicLvl` (an item level floor for magic copies) hid all magic stock below item level 25 (measured, p4-engine-b) |
| +24 | u8 | MagicMin | 0-1 |
| +25 | u8 | MagicMax | 3 |
| +40, +41 | u8 | flags only on Atma (91), Geglash (121) and Cain (3) rows of sacred item types | not a shop |

Always-in-stock rows become `Min = Max = 1`, like D2R's potions; equipment rows copy Min, Max, MagicMin and MagicMax. `MagicLvl` is 0 (magic copies from the item's base level on, as Median).

## The stock builder, Median and D2R

Both builders (D2Sigma `0x93a30`, D2R `0x53c9f0` called from the trade request `0x540850`) have the same shape: item level = player level + 5 capped by the act, every item needs its base `level` <= that level, normal copies (Min..Max) only while the item level is below 25, magic copies rolled in [MagicMin, MagicMax] (3 at most in the data). The stock is built once per game (and again when the NPC's restock flag, byte +0x35 of its shop state, is set).

What differed in D2R, measured with the game check `vendorstock` (Charsi, three difficulties, player levels 10 / 40 / 90, the builder's item level read from the cap function 0x53cfa0 through the `shop` trace; harness `shop <class> restock` rebuilds the stock for the player's current level):

| Rule | Median | D2R before | Now |
|---|---|---|---|
| Item level cap, Normal | MonLvlEx + 5 of the act's last waypoint area: 20 31 40 43 53 | actinfo `maxnpcitemlevel` 12 20 28 36 45 (Charsi at level 40: 12) | `waypoints_data.py` writes Median's values into actinfo (levels 35, 46, 101, 107, 129) |
| Item level cap, Nightmare | MonLvlEx(N) + 5: 68 80 88 93 102 | none (Charsi at level 90: 95) | plugin part `vendorstock` hooks the cap function 0x53cfa0 for difficulty 1 (`vendor_stock.cpp`) |
| Item level cap, Hell | none | none | same |
| Magic copies at item level 25+ | rolled in [MagicMin, MagicMax] | one or two more copies (up to 5 of one item; measured 5) | patch of the compare at 0x53cd2b (`cmp r13d, 0x19` -> 0x7f): at most 3 of one item |
| Magic stock below item level 25 | yes | none (MagicLvl 25 came from the wrong byte) | `txt_migrate.py` writes MagicLvl 0 |

Not compared: the quality weights of the 'good' rolls of the per-item generator 0x540ea0 (same call shape in both builders, no data to compare it with).

## Not converted

- Atma, Geglash and three Cain rows: sacred items with only the +40/+41 flags. They look like a D2Sigma service
  (not a D2R vendor; D2R has no Atma/Geglash vendor columns). Plugin work if it matters.
- The bytes not listed above are 0 or constant (+9..+11 are always 1) in every row.
