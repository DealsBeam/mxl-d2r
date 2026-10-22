# HD looks for Median-only monsters without new models (Phase 5)

Tool: `tools/hd_monsters.py` (runs in `tools/build.py` after `gfx_placeholders.py`). Report of every Median monster ->
donor + scale + colour + confidence: `work/mod/hd_monsters.md`.

## 1. What D2R's HD monster data contains

`data/hd/character/monsters.json` maps a monstats Id to a *unit name*; the unit is `data/hd/character/enemy|npc/<unit>.json`
(a `UnitDefinition`), all plain JSON:

| Piece | Where | Notes |
|---|---|---|
| Parts | one entity per (part, variant): `UnitPartComponent{part, variant}` + `ModelDefinitionComponent` (.model) or `UnitAttachmentComponent` (weapon/shield json) | monstats2 flags (HD TR LG RA LA RH LH SH S1-S8) and `*v` variant lists choose which (part, variant) is drawn; a variant the unit lacks is not drawn. Many entities may share one key: all are drawn (verified) |
| Skeleton, IK, aim layers | components of `entity_root` | |
| Scale | `TransformDefinitionComponent.scale` on `entity_root` (only 82 of 275 units have one) | works from any value |
| Animations | `state_machine_filename`: states with `modeId` (0 DT, 1 NU, 2 WL, 3 GH, 4 A1, 5 A2, 6 BL, 7 SC, 8-11 S1-S4, 12 DD, 13 KB, 14 SQ, 15 RN) and `animationBindings` keyed by weapon class (hth, 1hs, bow ...) | donor state machines cover all modes almost everywhere |
| Colour | `VariantDefinitionComponent` -> `*_variant.json`: 8 `VariantColorTransform` entries (monstats `TransLvl` indexes them), each 3 `ColorTransform` (the KTINT mask channels) | see below |

Variants in D2R itself: Fallen/Carver etc. share one unit and differ only by `TransLvl` -> variant entry; act variants and
champions likewise. Size variants are not separate units: the unit scale or the monster's own sizes.

`ColorTransform.colorAdjustment` = (hue shift in turns, saturation offset, value offset, weight); `colorTint` = (r, g, b,
amount) blended over. Verified in game on a zombie: x 0.33 turns the brown skin green, 0.66 purple, y changes saturation,
z -0.6 darkens, tint blends. Particles/emissive are `VfxDefinitionComponent`s of the donor, not authorable here.

## 2. Method

For each Median monster whose art D2R lacks (1,206; the 23 that fully borrow a D2R monster's graphics stay as
`gfx_placeholders.py` made them):
- Family tags from the Median Id/NameStr/MonType keywords (ids are descriptive: `undead_lancer_3`, `carrion_worm_2`), body plan
  (biped/quad/arachnid/serpent/flying/static) from tags or the art aspect. D2R donors (149 usable units) are tagged by name
  plus a hand table.
- Score: family, body plan, boss/non-boss, AI/MonType, animation modes of Median's monstats2 vs the donor state machine,
  weapon class, size (classic DCC height of both, NU frame, parts from monstats2 variants) and mean colour distance.
- Scale = the art size ratio (0.5-2.5) times the unit's own. Colour: mean colour of Median's DCC (units palette, palshift.dat
  of the TransLvl) vs the donor's classic art -> hue/sat/val offsets, plus a tint blend when a grey donor stands in for a
  clearly coloured monster. A new variant table holds it in all 8 entries.
- Parts: Median draws mostly only TR; donor parts without a slot are re-keyed into Median's flagged slots (all drawn), Median's
  variant names are cloned from the donor's nearest variant, weapons dropped when Median has no weapon slot.
- State machine: bound for Median's weapon class too when the donor lacks it.
- Median monsters that use D2R's own art (2,581): the unit of the same Id and code, else the best unit of that code. This
  fixes 107 that `gfx_placeholders.py` pointed at the first unit of the code (e.g. D2R archers shown as the plain rogue).

Result (2026-10-21): 790 high, 316 medium, 85 low confidence (low = nothing in the name; mostly bosses and odd names).
Output about 490 units and 420 colour tables.

Art provenance (tools/art_provenance.py, 2026-10-22): before the scoring, a Median monster whose COF parts are D2R DCC
animations (exact or recoloured, compared per part and variant) takes that vanilla unit as donor, with the recolour as the
tint. Of 4,211 Median monster DCCs 110 are exact and 11 recolours (Median-only: 45 and 11 of 4,130); the rest is new art. 34
monster rows moved to a provenance donor (33 exact, 1 recolour: observer x15 -> vampire1, the R% wanderers -> darkwanderer,
sapling_guard -> overseer1, treewarden_2 -> thornhulk1, ...); the old scoring remains for everything else. `--no-provenance`.
In game (slot 3, 2026-10-22): `h spawn` of observer, lost_traveler, sapling_guard, treewarden_2, wereowl_4, lilith_2 and
dimensional_gate loaded and drew with no new error class (screenshots in `checks-20261022-174622-s3`, not committed).

## 3. In game (background mode, build 93847)
`cheat goto` + `cheat mspawn <id>`: new units load, multi-part donors draw whole, scale and colour apply. Before/after
screenshots (not committed): `%USERPROFILE%\mxl-d2r-runs\hd-monsters-before-after-1.png` and `-2.png` (cabalist as a robed
priest, astrogha as a spider, tenebral as a wolf, undead lancer ...). Colour experiment: `checks-20261021-195552`.
A flying donor (wyvern -> wisp) and the viper were not visible in their shot: not verified.

### Pinned donors (2026-10-22)
`PINNED_DONORS` in `tools/hd_monsters.py` maps a Median Id to `(D2R donor unit, size factor)` and replaces the scoring for that
Id; colour (`tint_of`), scale (`score`'s art-size ratio times the factor), weapon-class state machine and part mapping stay as
for every other monster. Used for the Wereowl (code OW): `wereowl` and `corrupted_wereowl` -> goatman1 (before:
barricadewall1 and cantor1 x1.25), corrupted keeps its x1.25 over the plain one (scale 1.4 and 1.75). goatman1 has no
attack2 or cast clip, so A2 and SC play its attack1 / skill1 through the donor's state machine. This is the placeholder
for the real Wereowl model (tools/sprite3d/README.md); `tools/sprite3d/ingame_shots.py` spawns an Id and screenshots its modes.

Rebuilt (`tools/build.py`, validate 0 errors in both banks), installed on slot 2 and spawned in the Blood Moor
(`cheat mspawn`, slot 2, 2026-10-22): both load and draw as the goatman model in Median's colour, no new error class. What the
two Ids do: `wereowl` is a NecroPet (Align 1, friendly, 19 life): it stands in neutral and does not move, hits do no damage,
`h kill` plays the death clip (screenshots show the fall and the corpse). `corrupted_wereowl` is a BloodRaven boss (Normal
rows have no life, so any hit kills it): it comes running, plays its attack (earth_talons, A1) and dies on `h kill`. Neutral,
run, attack1 and death are seen for goatman1 on both; gethit (hit flag set, life taken under Hell rules, the animation itself
not caught in a picture) and cast (neither Id has an SC skill; Sk1 / Sk2 use A1) are not seen yet.

## 4. Real models later
Needs new `.model` meshes + skeleton + animations; each Median look then becomes its own unit using this tool's
unit/variant/state-machine plumbing instead of a donor. Priority: bosses and the families with the most monsters
(LostSoul, RemadeFlesh, Corrupted*, Walker).
