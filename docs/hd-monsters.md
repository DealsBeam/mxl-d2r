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

## 3. In game (background mode, build 93847)
`cheat goto` + `cheat mspawn <id>`: new units load, multi-part donors draw whole, scale and colour apply. Before/after
screenshots (not committed): `%USERPROFILE%\mxl-d2r-runs\hd-monsters-before-after-1.png` and `-2.png` (cabalist as a robed
priest, astrogha as a spider, tenebral as a wolf, undead lancer ...). Colour experiment: `checks-20261021-195552`.
A flying donor (wyvern -> wisp) and the viper were not visible in their shot: not verified.

## 4. Real models later
Needs new `.model` meshes + skeleton + animations; each Median look then becomes its own unit using this tool's
unit/variant/state-machine plumbing instead of a donor. Priority: bosses and the families with the most monsters
(LostSoul, RemadeFlesh, Corrupted*, Walker).
