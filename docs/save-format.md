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

## Character stats ("gf")
16-bit stat id, then the value in itemstatcost **CSvBits** (Median: strength-statpts 11, newskills 9, level 8,
experience 32, ...); the list ends with 0xffff.

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
