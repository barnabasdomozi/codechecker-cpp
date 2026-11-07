"""Renders the logo lines of captured terminal output as an SVG on a transparent
background.

Makes terminal-logo.svg, the logo at the top of the main README.md. To
remake it after changing src/common/cli/logo.cc, run from the repository
root (the logo only shows on a terminal at least 79 columns wide):

  script -qc "stty cols 120; build/release/bin/codechecker-cpp --help" \\
      /dev/null > raw.txt
  python3 docs/images/ansi2svg.py raw.txt docs/images/terminal-logo.svg
"""
import math
import re
import sys

CW, CH = 10, 20  # terminal cell size in px
PAD = 4          # margin around the logo, so the sparks at its edges fit


def xterm(code):
    if code < 16:
        raise ValueError(code)
    if code >= 232:
        v = 8 + (code - 232) * 10
        return f"#{v:02x}{v:02x}{v:02x}"
    code -= 16
    lv = [0, 95, 135, 175, 215, 255]
    r, g, b = lv[code // 36], lv[code // 6 % 6], lv[code % 6]
    return f"#{r:02x}{g:02x}{b:02x}"


def parse(raw):
    """Yields rows of (char, color) cells for the logo (lines up to the first blank)."""
    rows = []
    for line in raw.replace("\r", "").split("\n"):
        if not line.strip() and rows:
            break
        cells, color = [], None
        for part in re.split(r"(\x1b\[[0-9;]*m)", line):
            m = re.fullmatch(r"\x1b\[([0-9;]*)m", part)
            if m:
                p = m.group(1).split(";")
                color = xterm(int(p[p.index("5") + 1])) if "38" in p else None
            else:
                cells += [(ch, color) for ch in part]
        rows.append(cells)
    return rows


def star(cx, cy, r, inner, color, fill):
    pts = []
    for i in range(8):
        a = math.pi / 4 * i - math.pi / 2
        rr = r if i % 2 == 0 else r * inner
        pts.append(f"{cx + rr * math.cos(a):.2f},{cy + rr * math.sin(a):.2f}")
    style = f'fill="{color}"' if fill else f'fill="none" stroke="{color}" stroke-width="1.1" stroke-linejoin="round"'
    return f'<polygon points="{" ".join(pts)}" {style}/>'


def main(src, dst):
    rows = parse(open(src, encoding="utf-8").read())
    cols = max(len(r) for r in rows)
    w = PAD * 2 + cols * CW
    h = PAD * 2 + len(rows) * CH
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
        '<title>codechecker-cpp</title>',
    ]
    top = PAD
    for r, row in enumerate(rows):
        y = top + r * CH
        c = 0
        while c < len(row):
            ch, color = row[c]
            x = PAD + c * CW
            if ch in "█▀▄":
                # Merge runs of the same glyph and color into one rectangle.
                n = 1
                while c + n < len(row) and row[c + n] == (ch, color):
                    n += 1
                ry, rh = {"█": (y, CH), "▀": (y, CH / 2), "▄": (y + CH / 2, CH / 2)}[ch]
                out.append(f'<rect x="{x}" y="{ry}" width="{n * CW}" height="{rh}" fill="{color}"/>')
                c += n
                continue
            cx, cy = x + CW / 2, y + CH / 2
            if ch == "✦":
                out.append(star(cx, cy, 4.5, 0.3, color, True))
            elif ch == "✧":
                out.append(star(cx, cy, 4, 0.35, color, False))
            elif ch == "∙":
                out.append(f'<circle cx="{cx}" cy="{cy}" r="1.8" fill="{color}"/>')
            elif ch != " ":
                raise ValueError(f"unexpected glyph {ch!r}")
            c += 1
    out.append("</svg>")
    open(dst, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"{dst}: {w}x{h}, {len(rows)} rows x {cols} cols")


if __name__ == "__main__":
    main(*sys.argv[1:])
