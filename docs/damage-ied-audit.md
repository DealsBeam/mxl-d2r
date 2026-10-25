# Damage and innate elemental damage audit (Median XL 2.14.4)

Lane damage-ied, D2R 3.3 build 93847. Sources are `assets/median/2_txt`,
the existing plugin, and the combat / skill damage research cited below.
This audit was started before implementation; game measurements follow below.

## Rules and existing implementation

- Physical weapon damage uses the base's min/max (including the alternate
  two-handed or throwing pair), local enhanced damage and flat damage. Ethereal
  adds 25% to base damage in Median, versus D2R's native 50%; `combat.cpp`
  already replaces that operation. Off-weapon damagepercent, weapon mastery,
  and the native Strength/Dexterity damage bonuses contribute to
  the outgoing physical multiplier. These are distinct from spell mastery.
  See `subsystems/combat-stats.md` functions 0x1005c290 / 0x1005c4f0.
- Skills accumulate the five per-level damage steps (2–8, 9–16, 17–22,
  23–28, 29+). The synergy formula percentage is applied before mastery.
  BaseSynergy skills add the level/Energy/stat-485 formula implemented in
  `skill_damage_logic.h`; StaticDamage skills omit mastery. Physical spell
  damage takes magic/physical spell mastery, excluding its weapon component.
  `skill_damage.cpp` already replaces all four native damage calculators;
  `calc_codes.cpp` implements syn1–syn6 using skill-layered stats.
- Flat elemental damage is separate from physical enhanced damage. Its
  relevant elemental mastery applies through the native elemental damage
  path. Weapon damage fractions are controlled by SrcDam / 128. Skill
  elemental base damage and weapon elemental damage must not be confused.
- Innate damage is **data-driven passive damage**, not the tooltip marker.
  Median stat 415 (port 444), property pr341/function 27, encodes attribute
  and element in its layer and a display percentage in its value. It has no
  stat operation. Median stat 484 (port 513) is the IED multiplier.
  Property pr375 grants a hidden skill and syn1 for that skill; pr348 and
  its siblings supply flat elemental damage and the weapon-type marker
  Median 395 (port 424). The ordinary flat damage is additional to innate.
- Hidden skills 2000–2004 (port 2072–2076) add equal min/max fire, lightning
  or cold damage. Their base contribution is
  `syn1 * attribute * (100 + IED) / 10000`, with integer truncation.
  Fire uses Dexterity; lightning/cold have Strength and Dexterity variants.
  Marker 395 >= 3 selects `floor(syn1 * 16 / 10)` before multiplying the
  attribute; marker >= 4 disables these passives.
  Fire adds par5*syn2 and par6*syn3 contributions. Lightning Strength adds
  par5*syn2, par6*min(1,syn3), par7*min(1,syn4); lightning Dexterity adds
  par5*min(1,syn2), par6*syn3, par3*syn4. Each contribution is multiplied
  by its attribute and (100+IED), divided by 10000 separately, then summed.
  Cold has no such extra terms. Skill 2007 supplies a three-element variant with
  denominator 30000. This is not enhanced physical damage or spell damage.
- The port preserves these formulas in migrated skills.txt. A passive
  recalculation is essential when their inputs change. `stat_rules.cpp`
  previously explicitly refreshed passives after **Strength only**, to update
  Grit. The measured Dexterity mismatch and its fix are described below.

## Display versus hits

| Role | D2R stat IDs used by the port |
|---|---|
| Local item enhanced min/max damage | 18 / 17 |
| Weapon min/max, alternate min/max, throwing min/max | 21 / 22, 23 / 24, 159 / 160 |
| Off-weapon enhanced physical damage | 25 |
| Flat fire, lightning, magic, cold, poison min/max | 48–49, 50–51, 52–53, 54–55, 57–58 |
| Fire, lightning, cold, poison mastery | 329–332 |
| Magic / physical spell mastery | 357 |
| Innate display marker; IED multiplier; weapon marker | 444; 513; 424 |
| Skill-layered syn1–syn6 | 412–417 |

The innate item tooltip in `item_text.cpp` computes the marker's percentage
from the local player's attribute and IED. It is neither a measurement of
the passive stat nor a hit. It omits the passives' separate synergy additions
and weapon-marker branches. The character's flat damage stats expose the
passive output; hits additionally involve SrcDam, mastery, mitigation,
avoidance and random base damage. The existing `skilldmg` harness compares
the four skill calculators with their original D2R implementations, but
does not itself prove actual weapon hits. Cosmetic differences already
tracked in `unported.md` include enhanced-damage placement and weapon
attribute bonus lines.

## Measurements and changes

Control run `checks-20261025-190133-s3`, throwaway `IedCodex`, with
`MXL_CONTROL=innate` (the previous arithmetic and Strength-only refresh):

| Probe | Measured result |
|---|---|
| Hidden fire, syn1=100, actual Dex 205, IED 0 / 50 | 205 / 307, as the data formula |
| Actual Dex 206, IED 51 | 311, as the data formula |
| Marker 3 / 4, Dex 205, IED 0 | 328 / 0, as the 1.6 branch and disable branch |
| One Dexterity point, IED 50 | Dex 205→206, fire 307→0; explicit native refresh restores 309 |
| Restored fire 309, native weapon hit SrcDam 128 / 64 | raw fire 79104 / 39552; monster loses 312 / 156 whole life (including physical) |
| Client formatter, Str 100 / Dex 199 / IED 5, marker value 10 | 20 for Dex, 31 for Str/Dex; both show 10.5% |

The magic-quality item carried `97:2072=1` and `412:2072=100`.
Its generated +5 Dexterity is included in the measured attributes. The
native passive refresh restores damage without re-equipping or changing
requirements: the loss is not a failed requirement. The existing packet
hook called D2R's equipment/stat-spending path, then refreshed only after
Strength. The `innate` part now refreshes after every attribute packet,
using the same guarded native function as Grit; `statclick` still controls
the independent Shift-click count adjustment.

The formatter check explicitly sets and restores client-unit inputs:
server base writes of custom stat 513 do not transmit it to the client.
It validates the formatter rather than claiming a network synchronization
test. Earlier normal-quality synthetic fixtures are superseded; they did
not serialize their arbitrary property list to the client and must not be
used to infer a shipping item bug. The first real-weapon attempt in this
control was class-restricted and refused; subsequent tests use its proper
Necromancer class temporarily on the throwaway, restoring it afterward.

Classic tooltip disassembly, `D2Sigma.dll` 0x10077d80 (lane-private
Ghidra project `ghidra-lanes/damage-ied/sigma`, confirmed with `disasm.py`):
0x10077e42 truncates the effective floating percentage with `cvttsd2si`
before passing it to integer multiply/divide helper 0x10088e50. For
attribute 4, 0x10077e8a compares Strength and Dexterity and `cmovg` selects
their maximum; the port's sum is incorrect. For attribute 199, property
10 and IED 5, the classic display is 19 at 10.5%, while the prior port
computed 20. For Strength 100 / Dexterity 199 the combined marker should
also display 19, versus the prior port's 31. These display rules must
not be substituted for the passive's actual damage formula, which
multiplies the attribute and IED before its final division.

Normal run `checks-20261025-190428-s3`: all five requested checks PASS
(`skillscaling`, `grit`, `statclick`, `ied`, `iedtip`). The 572 skill-scaling
samples at eleven skill levels and four character setups match the Median
table formulas, including the existing plugin's deliberate differences
from original D2R damage. Grit and ordinary/Shift stat spending regressions
pass. Native innate weapon `!70` now gives fire 34→35 for Dex 199→200 at
IED 50. The synthetic magic item gives fire 309→310 for actual Dex 206→207
without a manual refresh. Native hits contain fire 79360 / 39680 at full /
half SrcDam and remove 312 / 156 whole life including physical. Both
formatter probes give 19 at 10.5%, matching the classic handler.

Final off-word control `checks-20261025-190827-s3`, same compiled binary
with `MXL_CONTROL=innate`: `ied` and `iedtip` both PASS their control
expectations. The naturally generated `!70` weapon drops from fire 34 to
5 after Dex 199→200: only its flat fire remains. The synthetic passive
drops 300→0 for Dex 200→201; manual refresh restores 301. Full / half
native hits then contain raw fire 77056 / 38528 and remove 304 / 151
whole life including physical. The formatter returns the former 20 / 31
at 10.5%. This isolates the `innate` part as the cause of the correction.

Independent-part run `checks-20261025-191004-s3`, `MXL_CONTROL=statclick`:
`ied` and `iedtip` PASS with Shift-click widening disabled. The `mxl` line
reports `innate damage on: attribute and equip refresh on`. Native weapon fire still
moves 34→35; synthetic fire moves 300→301 after Dexterity and remains 301
after Strength, Energy and Vitality points. This checks all four packet
attributes and confirms the refresh does not depend on enabling `statclick`.
Release build, all nine C++ test executables, Python compileall, and
`git diff --check` pass. PluginVersion remains 0.66.0.

## Lane ied-vanish: the equipment recheck and the equip hook (plugin 0.69.0)

The refresh after attribute spending (above) was not enough. D2R's equipment recheck takes every item off and puts it back through the item equip step
`0x475140`; the passives are applied (flag 0) at each step, and the last apply runs while the item's stat list is half back (oskill and Dexterity in, the
synergy stat 412:2072 not yet), so the innate fire ends at 0. The recheck runs in the handler of each attribute packet, again 0.4 - 2 s later, on gear changes
and when a character is loaded with such gear (fire 0 from the first frame with the part off, 205 with the fix). The `innate` part now hooks `0x475140` and applies
the passives again (`0x33f830(owner, 1)`) after it returns for a player. Evidence and checks: [testing.md](testing.md) "Lane ied-vanish". The formulas are not
changed; the control run (`MXL_CONTROL=innate`) still shows the original drop (`ied` expects the loss with the part off).

## Remaining scope and test limits

The lane proves and fixes passive invalidation after attribute spending,
and the two classic tooltip arithmetic mismatches. It does not replace the
already-ported skill damage formulas or broadly change native physical /
elemental combat. Independently measuring lightning/cold variants, their
extra synergy terms, the three-element aura, dual wielding, poison duration,
conversion, and selected-skill character-panel boxes remains follow-up
work; no numerical mismatch in those paths is claimed. Existing missing
weapon attribute-bonus lines and selected-skill panel descriptions remain
in `unported.md`. The unresolved alternate flag branch of classic descfunc
35 is not certified by these formatter probes.

All runs use slot 3 and only throwaway `IedCodex`. The normal run's
watchdog records zero cursor moves and zero foreground changes. It has
the same 38 loader ERROR/ASSERT lines seen in controls (map/startup and
shutdown pool assertions), no plugin log errors, and no new crash files.
Those pre-existing assertions are not a clean-log claim or fixed here.
The start-state REVIEW row records the skill-scaling fixture removing its
own gear/stance before Grit. No other slot, process, or character was altered.
