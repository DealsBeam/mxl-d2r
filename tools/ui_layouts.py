"""UI layout overrides (data/global/ui/layouts/*.json) for Median's interface.

skillstreepanelhd.json: Median has 8 skill pages per class (5 trees, masteries, paragon...; D2Sigma's
skilltabs.bin), each the same 6x3 grid as D2R's tree. D2R's panel has 3 TabWidgets that send
"SkillsTreePanelMessage:ActivateTab:<n>"; this layout has 8, in one row, labelled 1-8. Whether D2R's panel
code accepts tabs past 2 is what this tests (EXPERIMENT: if it does not, the extra tabs do nothing).
The per-tab background frames assume 3 tabs, so tabs here have no background image.

Inventory and cube (Phase 5): Median's player inventory is a 15 x 10 grid and its Horadric Cube 15 x 10 (D2R: 10 x 4
and 3 x 4). The HD panels get Median's own art (tools/ui_panels.py, hd/global/ui/panel/mxl/) and Median's classic positions
(inventory.bin, 512 x 704 panel, 29-pixel cells) scaled to D2R's 1162 x 1507 panel: x 2.2695, y 2.1406, so a cell
is 66 x 62 and the grid lines up with the art. Equipment slots move to Median's places; the weapon swap tabs sit
above Median's arm slots. The grid sizes themselves are in inventory.txt (txt_migrate.py, inventory_sizes).

Writes work/mod/data/global/ui/layouts/skillstreepanelhd.json, playerinventoryoriginallayouthd.json,
playerinventoryexpansionlayouthd.json and horadriccubelayouthd.json (after txt_migrate.py).
Usage: python tools/ui_layouts.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "work" / "mod" / "data" / "global" / "ui" / "layouts"
BASE = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "ui" / "layouts"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
TABS = 8
LEFT, RIGHT, TOP, HEIGHT = 130, 1018, 160, 99


def baseline(name: str) -> str:
    p = BASE / name
    if not p.exists():
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import native
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(native.casc_read(native.open_casc(GAME), f"data:data/global/ui/layouts/{name}"))
    return p.read_text("utf-8-sig")


def block_span(text: str, marker: str) -> tuple[int, int]:
    """Start/end of the {...} object containing marker (brace matching; the files have no braces in strings)."""
    i = text.index(marker)
    start = text.rindex("{", 0, i)
    depth, j = 0, start
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                end = j + 1
                if text[end:end + 1] == ",":
                    end += 1
                return start, end
        j += 1


def tabs_json() -> str:
    width = (RIGHT - LEFT) // TABS
    out = []
    for n in range(TABS):
        x = LEFT + n * width
        out.append(f"""        {{
            "type": "TabWidget", "name": "Tab{n}",
            "fields": {{
                "activationRect": {{ "x": {x}, "y": {TOP}, "width": {width - 4}, "height": {HEIGHT} }},
                "onClickMessage": "SkillsTreePanelMessage:ActivateTab:{n}",
            }},
            "children": [
                {{  // the stock code may look this up by name; its art assumes 3 tabs, so it sits off screen
                    "type": "ImageWidget", "name": "Background",
                    "fields": {{ "frame": {min(n, 2)}, "rect": {{ "x": -5000, "y": -5000 }} }}
                }},
                {{
                    "type": "TextBoxWidget", "name": "Label",
                    "fields": {{
                        "rect": {{ "x": {x}, "y": {TOP + 12}, "width": {width - 4}, "height": 76 }},
                        "style": "$StyleSkillTreeTabs",
                        "text": "{n + 1}",
                    }}
                }},
            ]
        }},""")
    for n in range(3):  # stock text boxes, kept (the code may look them up by name) but off screen
        out.append(f"""        {{
            "type": "TextBoxWidget", "name": "TextTab{n}",
            "fields": {{ "rect": {{ "x": -5000, "y": -5000, "width": 10, "height": 10 }}, "style": "$StyleSkillTreeTabs" }}
        }},""")
    return "\n".join(out) + "\n"


def skill_tree() -> str:
    t = baseline("skillstreepanelhd.json")
    for n in range(3):  # drop the stock tabs and their text boxes
        s, e = block_span(t, f'"name": "Tab{n}"')
        t = t[:s] + t[e:]
        s, e = block_span(t, f'"name": "TextTab{n}"')
        t = t[:s] + t[e:]
    s, _e = block_span(t, '"name": "Title"')
    return t[:s] + tabs_json().lstrip() + "        " + t[s:]


# Median's classic panel (512 x 704) -> D2R HD panel (1162 x 1507)
SX, SY = 1162 / 512, 1507 / 704
# Items draw at their HD size (98 pixels per cell) whatever cellSize says, so the grid keeps 98 x 92 cells and the
# whole widget is scaled to Median's 66 x 62 (first test: 66 x 62 cells left 98-pixel items overlapping).
CELL = '{ "x": 98, "y": 92 }'
GRID_SCALE = 0.6735
# Median inventory.bin (player rows), right panel: x is screen x, the panel starts at 512
INV_GRID = (551, 330)
INV_SLOTS = {  # widget: (left, right, top, bottom)
    "slot_right_arm": (614, 672, 111, 227), "slot_torso": (739, 797, 169, 256), "slot_left_arm": (865, 923, 111, 227),
    "slot_head": (739, 797, 91, 149), "slot_neck": (817, 846, 154, 183), "slot_right_hand": (690, 719, 251, 280),
    "slot_left_hand": (817, 846, 251, 280), "slot_belt": (739, 797, 276, 305), "slot_feet": (865, 923, 247, 305),
    "slot_gloves": (614, 672, 247, 305),
}
CUBE_GRID = (39, 208)          # Median's Transmogrify Box, left panel
STASH_GRID = (53, 149)         # Median's stash (Bank Page 1 / Big Bank Page 1), 14 x 14, left panel
STASH_TABS = (112, 86, 403, 134)  # where Median's gold box was (left, top, right, bottom)
STASH_GOLD = (118, 619)        # the box's coin after ui_panels.py moves it down (MOVES)
CUBE_BUTTON = (224, 580)       # the transmute button's place in Median's crafting.dc6
GOLD_ICON = (37, 636)          # the coin box at the left of Median's gold bar (character-inventory.dc6, y 635-662)
GOLD_TEXT = (62, 636)
RECT = re.compile(r'"rect":\s*\{[^}]*\}')


def hd(x: float, y: float) -> tuple[int, int]:
    return round(x * SX), round(y * SY)


def rect(x: int, y: int, w: int | None = None, h: int | None = None) -> str:
    return f'{{ "x": {x}, "y": {y}' + (f', "width": {w}, "height": {h}' if w is not None else "") + " }"


def set_rect(text: str, name: str, value: str) -> str:
    s, e = block_span(text, f'"name": "{name}"')
    return text[:s] + RECT.sub(lambda _m: f'"rect": {value}', text[s:e], count=1) + text[e:]


def set_fields(text: str, name: str, fields: str) -> str:
    """Replaces the "fields" object of the widget called name."""
    s, e = block_span(text, f'"name": "{name}"')
    block = text[s:e]
    i = block.index('"fields"')
    j = block.index("{", i)
    depth, k = 0, j
    while True:
        depth += {"{": 1, "}": -1}.get(block[k], 0)
        if depth == 0:
            break
        k += 1
    return text[:s] + block[:j] + fields + block[k + 1:] + text[e:]


def grid_fields(x: int, y: int, cols: int = 15, rows: int = 10) -> str:
    return (f'{{ "rect": {{ "x": {x}, "y": {y}, "scale": {GRID_SCALE} }}, "cellCount": {{ "x": {cols}, "y": {rows} }}, "cellSize": {CELL}, '
            f'"gemSocketFilename": "PANEL\\\\gemsocket", "chronicleFlourishOffset": {{ "x": -1, "y": 0 }} }}')


def inventory_original() -> str:
    t = baseline("playerinventoryoriginallayouthd.json")
    t = t.replace('"PANEL\\\\Inventory\\\\Classic_Background"', '"PANEL\\\\mxl\\\\inventory"')
    t = set_fields(t, "grid", grid_fields(*hd(INV_GRID[0] - 512, INV_GRID[1])))
    # Slots draw their frame and item at D2R's HD size too: keep D2R's size, scale like the grid, centre the result
    # on Median's slot.
    for widget, (l, r, top, b) in INV_SLOTS.items():
        x, y = hd(l - 512, top)
        x2, y2 = hd(r - 512, b)
        s_, e_ = block_span(t, f'"name": "{widget}"')
        m = re.search(r'"width":\s*(\d+),\s*"height":\s*(\d+)', t[s_:e_])
        w, h = int(m.group(1)), int(m.group(2))
        sw, sh = w * GRID_SCALE, h * GRID_SCALE
        cx, cy = round((x + x2 - sw) / 2), round((y + y2 - sh) / 2)
        t = set_rect(t, widget, f'{{ "x": {cx}, "y": {cy}, "width": {w}, "height": {h}, "scale": {GRID_SCALE} }}')
    bx, by = hd(*GOLD_ICON)
    tx, _ty = hd(*GOLD_TEXT)
    t = set_rect(t, "gold_button", rect(bx, by + 4))
    t = set_rect(t, "gold_amount_tooltip", rect(bx, by + 4))
    t = set_rect(t, "gold_amount", rect(tx, by + 5, 420, 48))
    return t


def inventory_expansion() -> str:
    t = baseline("playerinventoryexpansionlayouthd.json")
    t = t.replace('"PANEL\\\\Inventory\\\\Background"', '"PANEL\\\\mxl\\\\inventory"')
    # weapon swap tabs: centred over Median's arm slots (D2R's pair sits at x 99 / 850, 213 wide, y 179)
    right_x = hd(INV_SLOTS["slot_right_arm"][0] - 512, 0)[0] + 66 - 107
    left_x = hd(INV_SLOTS["slot_left_arm"][0] - 512, 0)[0] + 66 - 107
    shifts = {"99": right_x, "205": right_x + 106, "850": left_x, "956": left_x + 106}
    for name in ("background_right_arm_selected", "background_left_arm_selected", "background_right_arm",
                 "background_left_arm", "text_i_left", "text_ii_left", "text_i_right", "text_ii_right",
                 "weaponswap_right_arm", "weaponswap_left_arm"):
        if f'"name": "{name}"' not in t:
            continue
        s, e = block_span(t, f'"name": "{name}"')
        block = re.sub(r'"x":\s*(\d+)', lambda m: f'"x": {shifts.get(m.group(1), m.group(1))}', t[s:e], count=1)
        t = t[:s] + block + t[e:]
    return t


def cube() -> str:
    t = baseline("horadriccubelayouthd.json")
    t = t.replace('"PANEL\\\\Horadric_Cube\\\\HoradricCube_BG"', '"PANEL\\\\mxl\\\\cube"')
    t = set_fields(t, "grid", grid_fields(*hd(*CUBE_GRID)))
    t = set_rect(t, "convert", rect(*hd(*CUBE_BUTTON)))
    # D2R's cube animation sits where Median's grid is: off screen
    t = set_rect(t, "flourish", '{ "x": -5000, "y": -5000, "scale": 2 }')
    return t


def stash() -> str:
    """D2R's stash panel (personal, shared and its own gem/material/rune tabs): the personal and shared tabs get
    Median's stash art and its 14 x 14 grid; tabs, shared-stash arrows and gold stay D2R's (they sit above and below
    the grid). The extra tabs and the small cube view in them stay D2R's."""
    t = baseline("bankexpansionlayouthd.json")
    t = t.replace('"PANEL\\\\Stash\\\\StashPanel_BG_Personal"', '"PANEL\\\\mxl\\\\stash"')
    t = t.replace('"PANEL\\\\Stash\\\\StashPanel_BG_Shared"', '"PANEL\\\\mxl\\\\stash"')
    t = set_fields(t, "grid", grid_fields(*hd(*STASH_GRID), 14, 14))
    # Personal / Shared tabs (195 x 72 each) centred where Median's gold box was (ui_panels.py moves it down)
    x0, y0 = hd(*STASH_TABS[:2])
    x1, y1 = hd(*STASH_TABS[2:])
    t = set_rect(t, "BankTabs", rect((x0 + x1) // 2 - 195, (y0 + y1) // 2 - 36))
    # gold: D2R's button on the box's coin, the amount in the box's first bar
    gx, gy = hd(*STASH_GOLD)
    t = set_rect(t, "gold_withdraw", rect(gx, gy))
    t = set_rect(t, "gold_amount", rect(gx + 58, gy, 420, 48))
    return t


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "skillstreepanelhd.json").write_text(skill_tree(), encoding="utf-8")
    (OUT / "playerinventoryoriginallayouthd.json").write_text(inventory_original(), encoding="utf-8")
    (OUT / "playerinventoryexpansionlayouthd.json").write_text(inventory_expansion(), encoding="utf-8")
    (OUT / "horadriccubelayouthd.json").write_text(cube(), encoding="utf-8")
    (OUT / "bankexpansionlayouthd.json").write_text(stash(), encoding="utf-8")
    print(f"skillstreepanelhd.json: {TABS} tabs; inventory and cube 15 x 10, stash 14 x 14 on Median's panels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
