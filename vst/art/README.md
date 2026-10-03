# Page artwork

| File | What | Status |
|---|---|---|
| `tone3000-logo.png` | TONE3000 logo (full), shown in the TONE3000 strip | **placeholder** (a text wordmark) |
| `t3k-mark.png` | TONE3000's compact T3K mark, shown on a block whose tone came from TONE3000 | **placeholder** |
| `blank.png` | transparent: the "Local" state of the mark | final |

TONE3000's design requirements (https://www.tone3000.com/api#design-requirements, "Logo usage") ask for its
official logo and mark. Download them from that page ("Download TONE3000 Logos"), export the full logo as
`tone3000-logo.png` (light version, transparent, about 300x60) and the T3K mark as `t3k-mark.png` (about 128x52),
replace the files here, and rebuild (`vst/build.sh`). Users see the full logo (TONE3000 strip) before the T3K
marks (on the blocks), as the requirements ask.

The phone page uses the official logo too when the plugin folder has `art/tone3000-logo.svg` or `.png`
(tools/package.sh copies `tone3000-logo.png` there).
