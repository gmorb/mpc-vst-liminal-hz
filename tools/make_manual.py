#!/usr/bin/env python3
"""make_manual.py - the user manual PDF, from docs/manual-src/ (HTML + CSS) and the repo's own images.

    python3 tools/make_manual.py            ->  docs/Liminal-Hz-<major.minor>-User-Manual.pdf

Needs Python Playwright with its Chromium, and pypdf. Fonts: Titillium Web from the mpc-vst-plugins kit
(MPC_VST, default ../mpc-vst-plugins), as for the banners.

Filled in at build time: the version (VERSION), the factory preset table (kPresets in vst/liminal_hz_vst.cpp, so it
can't drift from the code), the numbered callouts on the page overview (positions from vst/layout.conf's widgets),
the signal-flow diagram, and the contents page numbers (a first print finds each chapter's page).
The cover is printed on its own without a page footer and put in front.
"""
import os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "manual-src")
FONTS = os.environ.get("MPC_VST", os.path.join(ROOT, "..", "mpc-vst-plugins")) + "/tools/html_art/fonts/"

VERSION = open(os.path.join(ROOT, "VERSION")).read().strip()
SHORT = ".".join(VERSION.split(".")[:2])
OUT = os.path.join(ROOT, "docs", f"Liminal-Hz-{SHORT}-User-Manual.pdf")

# where each numbered callout sits on the 1280x628 page (layout.conf coordinates, y - 86 for the page's top edge)
CALLOUTS = [
    (1, 312, 22), (2, 414, 30), (3, 20, 82), (4, 405, 92), (5, 20, 152), (6, 430, 152), (7, 92, 182),
    (8, 24, 288), (9, 22, 358), (10, 386, 358), (11, 254, 398), (12, 650, 16), (13, 580, 56), (14, 580, 222),
    (15, 580, 262), (16, 1142, 448), (17, 112, 424), (18, 262, 440), (19, 566, 424), (20, 880, 424), (21, 1092, 516),
]

SIGNAL_FLOW = """<svg viewBox="0 0 720 132" width="100%" xmlns="http://www.w3.org/2000/svg" font-family="Titillium Web" font-size="11">
<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0L10,5L0,10z" fill="#77726a"/></marker></defs>
<g fill="none" stroke="#77726a" stroke-width="1.2">
<path d="M28 46H52" marker-end="url(#a)"/><path d="M132 46H152" marker-end="url(#a)"/><path d="M246 46H266" marker-end="url(#a)"/>
<path d="M362 46H382" marker-end="url(#a)"/><path d="M446 46H466" marker-end="url(#a)"/><path d="M530 46H550" marker-end="url(#a)"/>
<path d="M600 46H620" marker-end="url(#a)"/><path d="M684 46H712" marker-end="url(#a)"/>
<path d="M314 62V92H200V62" stroke-dasharray="4 3" marker-end="url(#a)"/>
<path d="M14 46V116H575V62" marker-end="url(#a)"/>
</g>
<g fill="#f5f2ec" stroke="#b8894a" stroke-width="1"><rect x="52" y="30" width="80" height="32" rx="5"/><rect x="152" y="30" width="94" height="32" rx="5"/>
<rect x="266" y="30" width="96" height="32" rx="5"/><rect x="382" y="30" width="64" height="32" rx="5"/><rect x="466" y="30" width="64" height="32" rx="5"/></g>
<g fill="#fbf3e6" stroke="#b8894a" stroke-width="1.6"><rect x="550" y="30" width="50" height="32" rx="5"/><rect x="620" y="30" width="64" height="32" rx="5"/></g>
<g fill="#1d1c1a" text-anchor="middle"><text x="92" y="50">Pre-delay</text><text x="199" y="50">Convolution</text><text x="314" y="50">Low/High Cut</text>
<text x="414" y="50">Width</text><text x="498" y="50">Duck</text><text x="575" y="50">Mix</text><text x="652" y="50">Output</text></g>
<g fill="#77726a" font-size="9.5"><text x="0" y="40">in</text><text x="700" y="40">out</text>
<text x="257" y="106" text-anchor="middle">Feedback: the wet signal, darkened, back into the convolution</text>
<text x="294" y="128" text-anchor="middle">dry signal</text><text x="498" y="22" text-anchor="middle">turns the wet down while you play</text></g>
</svg>"""


def presets_table():
    src = open(os.path.join(ROOT, "vst", "liminal_hz_vst.cpp")).read()
    body = src[src.index("static const Preset kPresets[] = {"):]
    body = body[:body.index("};")]
    rows = re.findall(r'\{"([^"]+)", "([^"]+)", ([-\d, ]+)\}', body)
    if len(rows) < 19:
        sys.exit("make_manual: kPresets not found or changed shape")
    out = ['<table><tr><th>Preset</th><th>Kind</th><th class="num">Mix</th><th class="num">Pre-delay</th><th class="num">Low Cut</th>'
           '<th class="num">High Cut</th><th class="num">Width</th><th class="num">Output</th><th class="num">Fade In</th>'
           '<th class="num">Fade Out</th><th class="num">Feedback</th></tr>']
    for name, kind, nums in rows:
        mix, pre, lc, hc, width, length, outp, start, fin, fout, stretch, rev, fb = [int(x) for x in nums.split(",")]
        out.append(f'<tr><td class="k">{name}</td><td>{kind}</td><td class="num">{mix}%</td><td class="num">{pre} ms</td>'
                   f'<td class="num">{lc} Hz</td><td class="num">{hc / 1000:.1f} kHz</td><td class="num">{width}%</td>'
                   f'<td class="num">{outp:+d} dB</td><td class="num">{fin}%</td><td class="num">{fout}%</td><td class="num">{fb}%</td></tr>'
                   .replace("+0 dB", "0 dB"))
    out.append("</table>")
    return "\n".join(out)


def callouts():
    return "\n".join(f'<span class="mk" style="left:{x / 12.8:.2f}%;top:{y / 6.28:.2f}%">{n}</span>' for n, x, y in CALLOUTS)


def build_dir():
    d = tempfile.mkdtemp(prefix="manual-")
    shutil.copy(os.path.join(SRC, "manual.css"), d)
    os.makedirs(os.path.join(d, "fonts")); os.makedirs(os.path.join(d, "img"))
    for f in ("TitilliumWeb-Regular.ttf", "TitilliumWeb-SemiBold.ttf"):
        shutil.copy(FONTS + f, os.path.join(d, "fonts", f))
    for f in ("page-render", "ir-list", "my-presets", "shape", "phone"):
        shutil.copy(os.path.join(ROOT, "docs", "images", f + ".png"), os.path.join(d, "img", f + ".png"))
    shutil.copy(os.path.join(ROOT, "vst", "art", "texture.png"), os.path.join(d, "img", "texture.png"))
    return d


def render(d, pages):
    html = open(os.path.join(SRC, "manual.html")).read()
    html = (html.replace("{{version}}", VERSION).replace("{{version_short}}", SHORT)
            .replace("{{preset_table}}", presets_table()).replace("{{callouts}}", callouts())
            .replace("{{signal_flow}}", SIGNAL_FLOW))
    for k, v in pages.items():
        html = html.replace("{{p:%s}}" % k, str(v))
    html = re.sub(r"\{\{p:[^}]+\}\}", "", html)
    open(os.path.join(d, "manual.html"), "w").write(html)
    from playwright.sync_api import sync_playwright
    foot = ('<div style="width:100%;font:7pt Titillium Web,sans-serif;color:#9a958c;padding:0 0.7in;display:flex;'
            f'justify-content:space-between"><span>LIMINAL Hz · User Manual · {VERSION}</span><span class="pageNumber"></span></div>')
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page()
        pg.goto("file://" + os.path.join(d, "manual.html"))
        pg.wait_for_load_state("networkidle")
        pg.evaluate("document.fonts.ready")
        pg.pdf(path=os.path.join(d, "body.pdf"), format="Letter", print_background=True, prefer_css_page_size=True,
               display_header_footer=True, header_template="<div></div>", footer_template=foot)
        pg.pdf(path=os.path.join(d, "cover.pdf"), format="Letter", print_background=True, prefer_css_page_size=True,
               page_ranges="1")
        b.close()


def chapter_pages(d):
    """{chapter key: page number}, from the eyebrow lines of the printed body"""
    txt = subprocess.run(["pdftotext", "-layout", os.path.join(d, "body.pdf"), "-"], capture_output=True, text=True).stdout
    pages = {}
    for i, page in enumerate(txt.split("\f"), 1):
        m = re.search(r"^\s*(CHAPTER (\d+)|APPENDIX ([AB]))\s*$", page, re.M)
        if m:
            pages.setdefault(m.group(2) or m.group(3), i)
    return pages


def main():
    d = build_dir()
    render(d, {})
    pages = chapter_pages(d)
    render(d, pages)
    if chapter_pages(d) != pages:
        sys.exit("make_manual: page numbers moved between passes")
    from pypdf import PdfReader, PdfWriter
    w = PdfWriter()
    w.add_page(PdfReader(os.path.join(d, "cover.pdf")).pages[0])
    body = PdfReader(os.path.join(d, "body.pdf"))
    for pg in body.pages[1:]:
        w.add_page(pg)
    w.add_metadata({"/Title": f"Liminal Hz {SHORT} User Manual", "/Author": "Gm0rb",
                    "/Subject": "Convolution reverb for Akai MPC and Force"})
    w.compress_identical_objects()
    with open(OUT, "wb") as f:
        w.write(f)
    print(f"manual: {OUT} ({len(w.pages)} pages, {os.path.getsize(OUT) // 1024} KB); chapters at {pages}")
    shutil.rmtree(d)


if __name__ == "__main__":
    main()
