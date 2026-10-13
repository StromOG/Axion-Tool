#!/usr/bin/env python3
"""Generate icons_data.js for simulator.html from src/icon_assets.cpp.

Parses the `axion_icons[][66]` byte table and emits a JS array of 66-byte
arrays so the simulator's drawIcon() renders the same icons as the firmware.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "icon_assets.cpp"
OUT = ROOT / "icons_data.js"


def main() -> int:
    text = SRC.read_text(encoding="utf-8", errors="replace")

    # Isolate the initializer block of axion_icons[][66] = { ... };
    start = text.find("axion_icons")
    brace = text.find("{", start)
    # find matching closing brace of the whole initializer
    depth = 0
    end = brace
    for i in range(brace, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    block = text[brace : end + 1]

    # Strip // comments
    block = re.sub(r"//[^\n]*", "", block)

    # Grab every hex byte in order
    bytes_list = [int(h, 16) for h in re.findall(r"0x[0-9A-Fa-f]{1,2}", block)]
    if len(bytes_list) % 66 != 0:
        print(f"WARN: {len(bytes_list)} bytes not a multiple of 66", file=sys.stderr)

    icons = [bytes_list[i : i + 66] for i in range(0, len(bytes_list), 66)]

    lines = ["// AUTO-GENERATED from src/icon_assets.cpp by tools/gen_icons.py", "const iconsData = ["]
    for ic in icons:
        lines.append("  [" + ",".join(str(b) for b in ic) + "],")
    lines.append("];")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {OUT} with {len(icons)} icons ({len(bytes_list)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
