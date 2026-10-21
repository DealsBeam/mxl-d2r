"""UI layout overrides (data/global/ui/layouts/*.json) for Median's interface.

skillstreepanelhd.json: Median has 8 skill pages per class (5 trees, masteries, paragon...; D2Sigma's
skilltabs.bin), each the same 6x3 grid as D2R's tree. D2R's panel has 3 TabWidgets that send
"SkillsTreePanelMessage:ActivateTab:<n>"; this layout has 8, in one row. The tab pictures (tools/skill_tree_art.py:
Median's tab titles, sockets and arrows, 8 frames per class in skillBackgroundFile's sprite) are each tab's
"Background" image, frame n. Classic mode (skillstreepanel.json) gets the same 8 tabs in its tab column.

Inventory and cube (Phase 5): Median's player inventory is a 15 x 10 grid and its Horadric Cube 15 x 10 (D2R: 10 x 4
and 3 x 4). The HD panels get Median's own art (tools/ui_panels.py, hd/global/ui/panel/mxl/) and Median's classic positions
(inventory.bin, 512 x 704 panel, 29-pixel cells) scaled to D2R's 1162 x 1507 panel: x 2.2695, y 2.1406, so a cell
is 66 x 62 and the grid lines up with the art. Equipment slots move to Median's places; the weapon swap tabs sit
above Median's arm slots. The grid sizes themselves are in inventory.txt (txt_migrate.py, inventory_sizes).

Trade window: Median's trade.dc6 has two 7 x 15 grids side by side (inventory.bin rows 6 and 7: Trade Page 1 on the
left, Page 2 on the right; D2R stacks two 10 x 4 grids). The left one is "them", the right one "us" (to check in
game; swap the x values of TRADE_GRIDS and TRADE_BARS). Names and accept buttons go in the two header bars, gold
under each grid.
NPC stores: Median's "Monster" inventory (the stores' grid) is 15 x 15 under four tabs (store.dc6); D2R's vendor panel
gets that art, grid and tabs, and its repair / refresh buttons move below the grid.
Stash: its materials tab shows the Horadric Cube as a 3 x 4 recess (D2R), now 15 x 10 over Median's grid picture
(ui_panels.py, CUBE_VIEW), the inventory being the same 15 x 10 cube.

Writes work/mod/data/global/ui/layouts/skillstreepanelhd.json, skillstreepanel.json, playerinventoryoriginallayouthd.json,
playerinventoryexpansionlayouthd.json, horadriccubelayouthd.json, bankexpansionlayouthd.json, tradelayouthd.json and
vendorpanellayouthd.json (after txt_migrate.py).
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
CLASSIC_TABS = {"x": 231, "y": -132, "width": 90, "height": 108}  # D2R classic tab column (3 tabs of "height")


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
                {{  // the class's skillBackgroundFile sprite, frame n: tab strip with Median's titles, sockets, arrows
                    "type": "ImageWidget", "name": "Background",
                    "fields": {{ "frame": {n}, "rect": {{ "x": 132, "y": 160 }} }}
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


def classic_tabs_json() -> str:
    c = CLASSIC_TABS
    step = c["height"] * 3 / TABS
    out = []
    for n in range(TABS):
        out.append(f"""        {{
            "type": "TabWidget", "name": "Tab{n}",
            "fields": {{
                "activationRect": {{ "x": {c["x"]}, "y": {round(c["y"] + n * step)}, "width": {c["width"]}, "height": {round(step)} }},
                "onClickMessage": "SkillsTreePanelMessage:ActivateTab:{n}",
            }},
            "children": [
                {{  // skltree_<class>_back.dc6: set 0 = panel, sets 1-{TABS} = tab n-1 active (4 tiles each)
                    "type": "GridImageWidget", "name": "Background",
                    "fields": {{ "rect": {{ "x": 0, "y": -240 }}, "frames": 4, "rows": 2, "frame": {4 * (n + 1)} }},
                }},
            ]
        }},""")
    return "\n".join(out) + "\n"


def classic_skill_tree() -> str:
    """D2R's classic skill tree panel with Median's 8 tabs (the stock text boxes and tooltips are for 3 tabs)."""
    t = baseline("skillstreepanel.json")
    for n in range(3):
        s, e = block_span(t, f'"name": "Tab{n}"')
        t = t[:s] + t[e:]
        t = set_rect(t, f"TextTab{n}", '{ "x": -5000, "y": -5000, "width": 10, "height": 10 }')
    s, _e = block_span(t, '"name": "SkillCounter"')
    return t[:s] + classic_tabs_json().lstrip() + "        " + t[s:]


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
# Trade (trade.dc6): grids 7 x 15 at x 33 / 277, y 188; header bars (left 28-245, right 268-485) y 135-176
TRADE_GRIDS = {"them": (33, 188), "us": (277, 188)}
TRADE_BARS = {"them": (28, 245), "us": (268, 485)}
TRADE_BAR_Y = (135, 176)
TRADE_GOLD_Y = 628
# Store (store.dc6 page 0): 15 x 15 grid, tabs 37-475 x 134-173, the chest icon and bar of the gold at y 97-120
STORE_GRID = (39, 180)
STORE_TABS = (37, 134, 475, 173)
STORE_GOLD = (118, 97, 390)    # icon left, top, bar right
STORE_BUTTONS_Y = 1345         # D2R's repair / refresh buttons, below the 15 x 15 grid (HD pixels)
CUBE_VIEW = (177, 720, 810, 506)  # ui_panels.py: the cube view in the stash's materials tab (HD pixels)
CUBE_VIEW_SCALE = 0.55
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


def grid_fields(x: int, y: int, cols: int = 15, rows: int = 10, scale: float = GRID_SCALE) -> str:
    return (f'{{ "rect": {{ "x": {x}, "y": {y}, "scale": {scale} }}, "cellCount": {{ "x": {cols}, "y": {rows} }}, "cellSize": {CELL}, '
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
    t = t.replace('"PANEL\\\\Stash\\\\AdditionalStash\\\\PANEL_AdditionalStash_Consumables"', '"PANEL\\\\mxl\\\\consumables"')
    t = set_fields(t, "grid", grid_fields(*hd(*STASH_GRID), 14, 14))
    # the cube view (materials tab): 15 x 10 over the art ui_panels.py pasted; D2R's cover picture of the recess goes
    t = set_fields(t, "horadriccube_grid", grid_fields(CUBE_VIEW[0], CUBE_VIEW[1], 15, 10, CUBE_VIEW_SCALE))
    t = set_rect(t, "horadriccube_cover", '{ "x": -5000, "y": -5000 }')
    # Personal / Shared tabs (195 x 72 each) centred where Median's gold box was (ui_panels.py moves it down)
    x0, y0 = hd(*STASH_TABS[:2])
    x1, y1 = hd(*STASH_TABS[2:])
    t = set_rect(t, "BankTabs", rect((x0 + x1) // 2 - 195, (y0 + y1) // 2 - 36))
    # gold: D2R's button on the box's coin, the amount in the box's first bar
    gx, gy = hd(*STASH_GOLD)
    t = set_rect(t, "gold_withdraw", rect(gx, gy))
    t = set_rect(t, "gold_amount", rect(gx + 58, gy, 420, 48))
    return t


def trade() -> str:
    t = baseline("tradelayouthd.json")
    t = t.replace('"PANEL\\\\Trade\\\\MultiplayerTrade_BG"', '"PANEL\\\\mxl\\\\trade"')
    y0, y1 = (round(v * SY) for v in TRADE_BAR_Y)
    mid = (y0 + y1) // 2
    for who, (gx, gy) in TRADE_GRIDS.items():
        t = set_fields(t, f"grid_{who}", grid_fields(*hd(gx, gy), 7, 15))
        bx0, bx1 = (round(v * SX) for v in TRADE_BARS[who])
        t = set_rect(t, f"ready_{who}", rect(bx0 + 12, mid - 37))
        t = set_rect(t, f"name_{who}", rect(bx0 + 100, mid - 26, bx1 - bx0 - 112, 52))
        gold_x = round(gx * SX) + (round(203 * SX) - 309) // 2
        t = set_rect(t, f"gold_amount_{who}", rect(gold_x, round(TRADE_GOLD_Y * SY), 309, 53))
    return t


def vendor() -> str:
    t = baseline("vendorpanellayouthd.json")
    t = t.replace('"PANEL\\\\Vendors\\\\VendorShop_BG"', '"PANEL\\\\mxl\\\\store"')
    t = t.replace('"PANEL\\\\Vendors\\\\VendorForge_BG"', '"PANEL\\\\mxl\\\\store"')
    tx, ty0 = hd(STORE_TABS[0], STORE_TABS[1])
    ty = (ty0 + round(STORE_TABS[3] * SY)) // 2 - 37
    t = set_rect(t, "tabs", rect(tx, ty))
    t = set_fields(t, "grid", grid_fields(*hd(*STORE_GRID), 15, 15))
    for name in ("button_repair", "button_repair_all", "button_refresh"):
        s_, e_ = block_span(t, f'"name": "{name}"')
        x = re.search(r'"x":\s*(\d+)', t[s_:e_]).group(1)
        t = set_rect(t, name, rect(int(x), STORE_BUTTONS_Y))
    # gold: Median's chest icon and bar at the top of the panel instead of D2R's coin at the bottom
    gx, gy = hd(STORE_GOLD[0], STORE_GOLD[1])
    bar_w = round((STORE_GOLD[2] - STORE_GOLD[0]) * SX)
    t = set_rect(t, "gold_icon", '{ "x": -5000, "y": -5000, "width": 57, "height": 57 }')
    t = set_rect(t, "gold_amount", rect(gx + 70, gy + 2, bar_w - 100, 48))
    t = set_rect(t, "StashWidget", rect(gx, gy, bar_w, 58))
    return t


# Character sheet (stat-screen.dc6 page 0, 512 x 704): boxes in classic pixels (left, top, right, bottom)
CS_NAME = (115, 57, 352, 94)
CS_VITALS = {"hp": (70, 152, 172, 187), "mana": (70, 210, 172, 245), "stamina": (340, 152, 442, 187)}
CS_XP = (340, 210, 442, 245)
CS_XPBAR = (118, 99)
CS_ROWS = [(471, 494), (497, 520), (523, 546), (550, 573)]
CS_ATTR_VALUE, CS_ATTR_LABEL = (65, 108), (108, 197)     # left four rows: value box | label box
CS_RES_LABEL, CS_RES_VALUE = (315, 404), (404, 447)      # right four rows: label box | value box
CS_ATTRS = [("StrengthTitle", "StrengthStat", "StrengthPointButton"), ("DexTitle", "DexStat", "DexPointButton"),
            ("VitalityTitle", "VitalityStat", "VitalityPointButton"), ("EnergyTitle", "EnergyStat", "EnergyPointButton")]
CS_RES = [("FireResistanceTitle", "FireText", "Fire"), ("ColdResistanceTitle", "ColdText", "Cold"),
          ("LightningResistanceTitle", "LightningText", "Lightning"), ("PoisonResistanceTitle", "PoisonText", "Poison")]
CS_DEFENSE = (207, 553, 305, 574)       # the bar under the shield
CS_DEFENSE_TITLE = (207, 579, 305, 600)
CS_SKILLS = {"left": 91, "right": 321}  # x of the 99-pixel boxes beside the two weapon portraits
CS_SKILL_ROWS = {"Damage": (351, 374), "Rating": (378, 401)}
CS_POINTS = (60, 618, 455, 650)
OFF = -5000


def span_n(text: str, marker: str, n: int = 0) -> tuple[int, int]:
    """Like block_span, for the n-th (0-based) widget with that marker."""
    i = -1
    for _ in range(n + 1):
        i = text.index(marker, i + 1)
    start = text.rindex("{", 0, i)
    depth, j = 0, start
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return start, j + 1
        j += 1


def edit(text: str, name: str, fn, n: int = 0) -> str:
    s, e = span_n(text, f'"name": "{name}"', n)
    return text[:s] + fn(text[s:e]) + text[e:]


def put(text: str, name: str, x: int, y: int, w: int | None = None, h: int | None = None, n: int = 0,
        size: int | None = None, align: str | None = None, label: str | None = None, scale: float | None = None) -> str:
    """Moves/resizes the widget (its first "rect"), optionally with a numeric point size, horizontal alignment,
    a literal text and a scale."""
    def fn(b: str) -> str:
        r = rect(x, y, w, h)
        if scale:
            r = r[:-2] + f', "scale": {scale} }}'
        b = RECT.sub(lambda _m: f'"rect": {r}', b, count=1)
        if size:
            b = re.sub(r'"pointSize":\s*"\$\w+"', f'"pointSize": {size}', b)
        if align:
            b = re.sub(r'("alignment":\s*\{\s*"h":\s*)"\w+"', rf'\1"{align}"', b)
        if label:
            b = re.sub(r'"text":\s*"[^"]*"', f'"text": "{label}"', b, count=1)
        return b
    return edit(text, name, fn, n)


def cbox(b: tuple) -> tuple[int, int, int, int]:
    x, y = hd(b[0], b[1])
    x2, y2 = hd(b[2], b[3])
    return x, y, x2 - x, y2 - y


def char_sheet() -> str:
    """D2R's character panel with Median's statsheet page 0 as background (ui_panels.py, PANEL\\mxl\\charsheet) and
    D2R's stat text widgets moved into its empty boxes. The attack rows lose their two-line labels (D2R's
    "ATTACK Damage" / "ATTACK Rating" for the left and right skill; the boxes are small and Median marks them with
    icons), the resistances get short labels."""
    t = re.sub(r'"name":\s+"', '"name": "', baseline("characterstatspanelhd.json"))  # the right skill's widgets have 2 spaces
    t = t.replace('"PANEL\\\\Character_Sheet\\\\Character_Background"', '"PANEL\\\\mxl\\\\charsheet"')
    # name bar: name left, "Level n Class" right
    x, y, w, h = cbox(CS_NAME)
    t = put(t, "CharacterName", x + 10, y, 260, h)
    t = put(t, "FullClassInfo", x + 270, y, w - 280, h, size=20, align="right")
    # flanking bars: life, mana, stamina (title, current / max), experience
    for key, (cur, mx, sep, title) in {"hp": ("CurrentHP", "MaxHP", 1, "HPTitle"),
                                       "mana": ("CurrentMana", "MaxMana", 2, "ManaTitle"),
                                       "stamina": ("CurrentStamina", "MaxStamina", 0, "StaminaTitle")}.items():
        X, Y, W, H = cbox(CS_VITALS[key])
        u = W / 230
        t = put(t, title, X, Y, round(96 * u), H, size=20)
        t = put(t, cur, X + round(96 * u), Y, round(56 * u), H, size=20)
        t = put(t, "separator", X + round(152 * u), Y, round(16 * u), H, n=sep, size=20)
        t = put(t, mx, X + round(164 * u), Y, round(60 * u), H, size=20)
    X, Y, W, H = cbox(CS_XP)
    t = put(t, "CombinedXP", X, Y, W, H, size=20, align="center")
    t = edit(t, "XpProgress", lambda b: RECT.sub(
        lambda _m: f'"rect": {{ "x": {hd(*CS_XPBAR)[0]}, "y": {hd(*CS_XPBAR)[1]}, "scale": 0.6 }}', b, count=1))
    # attributes: value in the narrow box, label in the wide one, the +1 button just right of the row
    for (title, stat, button), (r0, r1) in zip(CS_ATTRS, CS_ROWS):
        t = put(t, title, *cbox((CS_ATTR_LABEL[0], r0, CS_ATTR_LABEL[1], r1)), size=20)
        t = put(t, stat, *cbox((CS_ATTR_VALUE[0], r0, CS_ATTR_VALUE[1], r1)), size=24)
        bx, by = hd(CS_ATTR_LABEL[1] + 3, (r0 + r1) / 2)
        t = put(t, button, bx, by - 23, scale=0.55)
    # resistances
    for (title, text, label), (r0, r1) in zip(CS_RES, CS_ROWS):
        t = put(t, title, *cbox((CS_RES_LABEL[0], r0, CS_RES_LABEL[1], r1)), size=20, label=label)
        t = put(t, text, *cbox((CS_RES_VALUE[0], r0, CS_RES_VALUE[1], r1)), size=22)
    # defense: value in the bar under the shield, label below it
    t = put(t, "ArmorClass", *cbox(CS_DEFENSE), size=24)
    t = put(t, "ArmorClassTitle", *cbox(CS_DEFENSE_TITLE), size=20)
    # attack stats of the left / right skill: values only, beside the weapon portraits
    for n, (side, left) in enumerate(CS_SKILLS.items()):
        t = put(t, f"{side.capitalize()}SkillStats", 0, 0)
        for group, (r0, r1) in CS_SKILL_ROWS.items():
            X, Y, W, H = cbox((left, r0, left + 99, r1))
            t = put(t, group, X, Y, W, H, n=n)
            half = H // 2
            t = put(t, f"Attack{group}Stat", 0, 0, W, H, n=n, size=20)
            t = put(t, f"Attack{group}StatTop", 0, 0, W, half, n=n, size=16)
            t = put(t, f"Attack{group}StatBottom", 0, half, W, H - half, n=n, size=16)
            t = put(t, f"Attack{group}Title", OFF, OFF, 10, 10, n=n)
    # stat points left at the bottom of the panel
    t = put(t, "CombinedPointsInfo", *cbox(CS_POINTS), size=22)
    return t


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "characterstatspanelhd.json").write_text(char_sheet(), encoding="utf-8")
    (OUT / "skillstreepanelhd.json").write_text(skill_tree(), encoding="utf-8")
    (OUT / "skillstreepanel.json").write_text(classic_skill_tree(), encoding="utf-8")
    (OUT / "playerinventoryoriginallayouthd.json").write_text(inventory_original(), encoding="utf-8")
    (OUT / "playerinventoryexpansionlayouthd.json").write_text(inventory_expansion(), encoding="utf-8")
    (OUT / "horadriccubelayouthd.json").write_text(cube(), encoding="utf-8")
    (OUT / "bankexpansionlayouthd.json").write_text(stash(), encoding="utf-8")
    (OUT / "tradelayouthd.json").write_text(trade(), encoding="utf-8")
    (OUT / "vendorpanellayouthd.json").write_text(vendor(), encoding="utf-8")
    print(f"skillstreepanel(hd).json: {TABS} tabs; inventory and cube 15 x 10, stash 14 x 14, trade 7 x 15 x 2, stores 15 x 15 on Median's panels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
