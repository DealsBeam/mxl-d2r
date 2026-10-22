# D2R 3.3 save format (v105) as the port uses it

Found by decoding real saves (tools/save/d2item.py, d2s_stats.py); written for hand edits of test characters. Always back
up first (`Saved Games/Diablo II Resurrected/mxl-manual-backups/`) and edit only while the game is closed.

## Header and checksum
- Signature 0xaa55aa55, version 0x69 at +4, file size at +8, checksum at +0xc (rotate-left-1 + byte sum over the
  file with the checksum zeroed).
- Class at +0x18, level (display) at +0x1b, name at +0x12b.
- D2RLoader's sidecar `<name>.d2rl`: `D2RL`, u16 version 2, u16 header size 0x2c, u32 body length at +0x08,
  SHA-256 of the body (+0x2c to the end) at +0x0c. The body repeats the save's checksum (+0x34) and size (+0x38) and
  holds a SHA-256 of the whole `.d2s` at +0x3c. After an edit update all four (checksum, size, save hash, then the
  body hash); a mismatch makes the character silently disappear from character select.

## Waypoints ("WS")
`WS`, u32 1 (version), u16 0x50 (size), then 3 x 24 bytes, one per difficulty: u16 `0x0102`, 5 bytes of Diablo II's 39 bits
(little-endian, bit n = waypoint n), 17 spare bytes. D2R 3.3 keeps a 112-bit record: it reads and writes the first 16 bytes
(header + 14 bytes = waypoint numbers 0-111; number n is bit `n % 16` of the u16 at +2 + 2 * (n / 16)) and writes zeros in the
other 8. Median's numbers 50-90 are stored there like any other (docs/subsystems/areas-drlg.md, "Waypoints"); the `.d2rl`
sidecar does not carry them. Bit 0 (Rogue Encampment) is set on every load.

## Character stats ("gf")
16-bit stat id, then the value in itemstatcost **CSvBits** (Median: strength-statpts 11, newskills 9, level 8,
experience 32, ...); the list ends with 0xffff.

**Widths cap the unspent points (checked 2026-10-23, lane release-055-fixes).** D2RLoader's extended save mode follows
CSvBits and does not narrow it: `statpts` (stat 4, unused stat points) is 11 bits (max 2047) and `newskills` (stat 5, unused skill
points) is 9 bits (max 511), the same as Median's own `itemstatcost.txt`. A value above the maximum is saturated on Save and Exit
(2327 stat points came back as 2047, 567 skill points as 511). A level 150 character earns 745 stat points (5 per level) and 149
skill points, so the widths hold everything a character can earn; only a test that piles on leftover points reaches the cap.
D2RLoader would take wider fields (the width is read from the column), but the port keeps Median's, so nothing is widened.

## Stats in practice (2026-10-14)
The "gf" list in these saves uses 16-bit stat ids (tools/save/d2s_stats.py still reads 9 bits and fails on them).
Barbarian growth (charstats): LifePerLevel 120 and ManaPerLevel 20 (quarter points), ManaPerMagic 6, StatPerLevel 5;
life/mana/stamina are stored <<8. experience.txt row L is the experience at which level L+1 starts (row 9 = 37000 =
level 10). barby was set to level 10 this way (all 45 stat points in energy, 9 unspent skill points); backup in
mxl-manual-backups/barby-before-level10.

## Class skills ("if")
"if", then one byte (hard points) per class skill: 95 in Median (95 rows per charclass in skills.txt), in the
order of the class's skills by skill ID. Barbarian slot 40 is whirlwind_2 (ID 1306). Slot 78 holds 1 on every new
character (a hidden class passive, e.g. skills_1272 / skills_1358). Hand-setting a byte works with the checksum and
`.d2rl` updates above (barby got Whirlwind this way, 2026-10-14; backup in mxl-manual-backups/barby-before-whirlwind).

## Items ("JM" + u16 count, then the items)
Bit-packed, LSB first, each item padded to a byte:

| Field | Bits | Notes |
|---|---|---|
| flags | 32 | 0x10 identified, 0x800 socketed, 0x10000 ear, 0x20000 starter, 0x200000 simple, 0x400000 ethereal, 0x1000000 personalized, 0x4000000 runeword; saved items also carry 0x80000 and 0x800000 |
| (new in 3.3) | 8 | 0x05 on every item seen |
| version | 3 | 5 |
| location, equip slot, x, y, page | 3, 4, 4, 4, 3 | stored 0 / equipped 1 / belt 2; page 1 inventory |
| code | Huffman, 4 chars | D2R's item-code tree (d2item.py `T`) |
| simple items | 1 + 128, then 1 | unique-id flag + 128-bit id, then the socket bit; done |
| sockets filled | 3 | |
| item id | 32 | |
| item level, quality | 7, 4 | |
| picture flag (+3), class-specific flag (+**16**) | | auto-affix id is 16 bits in 3.3 (11 in 2.x) |
| quality fields | | low/superior 3, magic prefix+suffix, set/unique id, rare names (widths for 3.3 not verified) |
| unique-id flag + id | 1 + 128 | |
| armour defense | itemstatcost armorclass Save Bits/Add (Median 16, +500) | |
| max durability, durability | Save Bits/Add of 73/72 (Median 9 +90, 9 +150) | durability only when max > 0 |
| timestamp bit | 1 | after the defense/durability fields in 3.3 |
| quantity, total sockets | 9, 4 | when stackable / socketed |
| stats | 16-bit id, Save Param Bits, Save Bits (value + Save Add) | ends with 0xffff |

## Corpse, mercenary and golem lists (after the items)
After the player's item list: `JM` + u16 corpse count (0 or 1); a corpse has a 12 byte header (u32 0, u32 x, u32 y) and a second `JM`
item list of the items it holds; then `jf` and the mercenary's `JM` item list, then `kf` (golem). Seen in a test save after a death:
`4a 4d 01 00 | 00 00 00 00 14 16 00 00 92 15 00 00 | 4a 4d 01 00 ...` = one corpse at (5652, 5522) with one item. With the plugin part
`nocorpse` (docs/subsystems/death.md) a dying character no longer gets one: the count stays 0. A save that already holds a corpse
loads through D2R's own path and the corpse can be picked up as before.

## Warning: hand-written items are rejected (2026-10-06)

The mod runs D2RLoader's **extended** save format (`[d2rcore.save_format] mode = "extended"`), which stores wider
item, stat, affix and skill IDs than retail 3.3. Two hand-built items (a +1 Whirlpool sword/axe, stat 97 with a
12-bit skill parameter) made D2R drop the character from character select, even with the save checksum, size and
both `.d2rl` hashes correct. A `.d2rl` mismatch alone only greys a character out (D2RLoader guide, "Environment
badges"). The skill-parameter width (and possibly other fields) differs in the extended format and is not known
yet. Until a game-written item with a skill stat has been decoded, get test items with D2RLoader's debug-mode Item
Spawner instead of editing saves. Paladin saves also hold a starter item whose code does not decode with the
retail Huffman table (flags 0xaa2010 at inventory 8,3), another sign that extended items differ.
