"""UI layout overrides (data/global/ui/layouts/*.json) for Median's interface.

skillstreepanelhd.json: Median has 8 skill pages per class (5 trees, masteries, paragon...; D2Sigma's
skilltabs.bin), each the same 6x3 grid as D2R's tree. D2R's panel has 3 TabWidgets that send
"SkillsTreePanelMessage:ActivateTab:<n>"; this layout has 8, in one row, labelled 1-8. Whether D2R's panel
code accepts tabs past 2 is what this tests (EXPERIMENT: if it does not, the extra tabs do nothing).
The per-tab background frames assume 3 tabs, so tabs here have no background image.

Writes work/mod/data/global/ui/layouts/skillstreepanelhd.json (after txt_migrate.py).
Usage: python tools/ui_layouts.py
"""
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


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "skillstreepanelhd.json").write_text(skill_tree(), encoding="utf-8")
    print(f"skillstreepanelhd.json: {TABS} tabs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
