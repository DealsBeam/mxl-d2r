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
| +19 | u8 | MagicLvl | 25 on every equipment row |
| +24 | u8 | MagicMin | 0-1 |
| +25 | u8 | MagicMax | 3 |
| +40, +41 | u8 | flags only on Atma (91), Geglash (121) and Cain (3) rows of sacred item types | not a shop |

Always-in-stock rows become `Min = Max = 1`, like D2R's potions; equipment rows copy the five values.

## Not converted

- Atma, Geglash and three Cain rows: sacred items with only the +40/+41 flags. They look like a D2Sigma service
  (not a D2R vendor; D2R has no Atma/Geglash vendor columns). Plugin work if it matters.
- The bytes not listed above are 0 or constant (+9..+11 are always 1) in every row.
