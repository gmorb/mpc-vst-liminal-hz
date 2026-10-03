# Liminal Hz for MPC OS: handoff summary

Paste into a new chat with `mpc-vst-liminal-hz-1.0.0-repo.zip`. Companion to NAM A2 (same author, same patterns).

## What it is
Convolution reverb VST2 for Gen 1 Akai Force/MPC (RK3288, ARMv7 A17 NEON, MPC OS). Name "Liminal Hz", manufacturer
"Gm0rb", uid NmIR, so liminal_hz.so, folder "Gm0rb - VST - Liminal Hz". Intended repo gmorb/mpc-vst-liminal-hz (unpublished),
catalog id liminal-hz (packaging/catalog-liminal-hz.json, entry check OK). Version 1.0.0. MIT. Kit mpc-vst-plugins 081c247.

## Design
- engine/long_convolver: head taps [0,4096) uniform-partitioned in 128-sample blocks on the audio thread (FFT 256);
  tail [4096,N) uniform-partitioned in W=2048 blocks (FFT 4096) on a worker thread: input W-block j -> tail of output
  block j+2, so each tail block has a whole W-period (46 ms) of budget. sem_post handoff, 3-slot output ring, misses
  counted (a late block plays without its tail). Modes: Threaded (used), Inline and Uniform (bench references).
- engine/reverb_engine: in -> pre-delay (0-250 ms) -> convolution (mono IR: L,R separately; stereo IR: mono sum into
  the IR's L and R) -> low cut / high cut (RBJ biquads, wet only) -> width (M/S) -> mix -> output. IR prep off the audio
  thread: AudioDSPTools ResampleCubic to host rate, cap 5 s, Length (5-100%), raised-cosine fade-out, unit-energy
  normalisation. New IR crossfades over 4096 samples; retired IRs freed by collect() (never on the audio thread). No IR:
  dry passes untouched; the wet share ramps linearly (an exponential ease stalled in float at -78 dB dry: fixed).
  Host blocks not a multiple of 128: FIFO (one block of latency).
- Shared with NAM A2 (copied): wav_reader (stereo kept), library ("reverbs" folders), tone3000 (one catalogue:
  format=ir&gears=space_outboard; downloads to reverbs/TONE3000/<tone>/), http, pkce. Params (18, fixed order):
  mix, predelay, lowcut, highcut, width, length, output, ir, ir_prev, ir_next, ir_info, ir_pack, ir_pack_prev,
  ir_pack_next, ir_src, ir_browse, rescan, t3k_info. Page: vst/layout.conf (browser renderer) + vst/skin_post.py.

## Measured on a Force (user, 2026-10-01; tools/bench_reverb.cpp, real time, % of a 2.9 ms block)
Threaded: 5 s stereo 3.4% mean / 4.7% p99 / 7.2% max on the audio thread, worker 8.3% of a core, 0 misses; 1 s stereo
3.7/5.3/7.7%, worker 2.4%. Uniform (cab method): 1 s mono 16%, 3 s mono 50%. Inline tail: up to 137% spikes.

## Tests (tools/test.sh): all PASSED
convolution vs direct (131-133 dB, x86 + ARM), engine_test (13), pkce, http ABI (compile-time), library,
plugin_test (23, incl. TONE3000 mock requiring gears=space_outboard); TSan, ASan clean; ARM plugin_test under qemu;
both install routes (vstscanner, catalog install.sh incl. upgrade keeping reverbs/) on a simulated Force.

## Open
1. Device test of the plugin itself (page, browsing, TONE3000 download of the Green-Wood Catacombs IR).
2. Worker thread priority: normal (SCHED_OTHER); misses are shown on the detail line ("Tail late N").
3. TONE3000 logo/mark are placeholders (vst/art/README.md); publish + catalog PR as NAM A2.

## 2026-10-01: DELETE (both plugins)
Param(s) appended: ir_delete (18) (momentary). Two taps within 4 s (first arms: info line "Tap DELETE again to remove it");
the worker calls Library::trash: rename to <parent of the models/irs/reverbs folder>/Deleted/<folder>/<pack>/file
(same drive), " (2)" on name clashes, tone3000.json moves with a pack's last audio file, empty pack folder removed;
library skips folders named Deleted. Then rescan; the selection lands on the next file. Tests: library_test (trash),
plugin_test (one tap asks / expires, two taps move, list moves on, rescan doesn't bring it back). tools/test.sh now
rebuilds the PC plugin when any source is newer (a stale build hid one run's failures). All suites, TSan, ASan PASS.

## 2026-10-01: DELETE is permanent (user's request)
Library::remove (replaces Library::trash): unlink the listed .nam/.wav (refused unless the path is
<library folder>/<display><ext>, as scan() lists it, and the extension is .nam/.wav); a pack's last audio file also
takes its tone3000.json and the empty folder. Prompt "Tap DELETE again to delete it permanently", notice "Deleted".
Folders named Deleted are still skipped by scans (files moved there by the earlier version stay out of the list).

## 2026-10-01: brand "Gm0rb"
vst.json vendor "Gm0rb" (was "NAM"): MPC lists the plugin under manufacturer Gm0rb; page/plugin folder
"Gm0rb - VST - Liminal Hz" (MPC finds a page by "<manufacturer> - VST - <name>"). uid, product name, catalog id, repo name unchanged.
Projects saved with the earlier "NAM" builds (test projects only) may need the plugin re-inserted.

## 2026-10-01: renamed IR Reverb -> Liminal Hz
In MPC: "EchoVault IR" (12 characters: the insert slot shows about 10-11 in large type under the manufacturer line)
by "Gm0rb". Product / README / catalog / zip / repo: "Liminal Hz" (mpc-vst-liminal-hz, catalog id
liminal-hz, asset Liminal-Hz-*-mpc-armv7.zip). Folder "Gm0rb - VST - Liminal Hz", liminal_hz.so,
vst/liminal_hz_vst.cpp; uid NmIR unchanged. Page tab / Q-Link bank "VAULT", block "IR". Tagline: "Real rooms,
halls and echoes from impulse responses: the space for your NAM amp."
Bug found by plugin_test and fixed: set_chunk compared the state header with a hard-coded length (12, the old
"ir-reverb=1\n"), so the new 13-character header refused every project. Now by the header's own length, and the
old header is accepted too (test projects from before the rename open). All suites, TSan, ASan, ARM PASS; real
vstscanner registers "Echo Vault" by "Gm0rb"; catalog zip 0 warnings.

## 2026-10-01: MPC name "EchoVault IR"
In MPC (plugin list, insert slot): "EchoVault IR" by "Gm0rb" (folder "Gm0rb - VST - Liminal Hz"; catalog zip
EchoVault-IR-<v>-mpc-armv7.zip, asset_pattern updated). The catalog lists the registry name "Liminal Hz"
(catalog_build uses the entry's "name"). uid NmIR, liminal_hz.so and the state header unchanged.

## 2026-10-01: phone page ports
NAM A2 8091, Liminal Hz 8191; start() binds the first free of base..base+7 (before the server thread;
the status shows the port in use; "Ports a-b in use" if none). serving_ flag: a page that closed itself (idle)
reopens on the next press (it used to keep showing the address with no server). MPCNAM_T3K_IDLE for tests.
plugin_test: two instances get base and base+1, both answer; idle close and reopen. Host, TSan, ASan, device PASS.

## 2026-10-01: MPC name "Liminal Hz" (device finding)
On the Force, "EchoVault IR" showed in the insert slot as "Echo.ault IR" (the V replaced by a dot), though the name's
bytes are plain ASCII and MPC shows wider names in full ("Delay Analog Syn", 16 characters, by Akai). So not width:
either MPC treats a camelCase join specially or the slot font lacks a capital V. Renamed to "Liminal Hz" (also
the catalog name): if it shows correctly it was the camelCase join; "Echo .ault IR" would mean the glyph.
Folder "Gm0rb - VST - Liminal Hz"; catalog zip Liminal-Hz-<v>-mpc-armv7.zip (asset_pattern updated).

## 2026-10-02: IR shaping, SHAPE display, textured page
- IRShape (engine/reverb_engine.h): start, length (End), fade_in, fade_out (default 10%, min 32 samples), stretch
  (resample to host_rate x stretch), reverse (after the 5 s cap); order: stretch, cap, reverse, trim, fades, level.
  Feedback: last block's wet (tanh) x 0..0.9 into the convolution input. engine_test: exact spike positions.
- Params 19-24: start, fade_in, fade_out, stretch, reverse, feedback; shape changes re-prepare the IR (shape_valid,
  loaded_shape). Params 25-49: wave_0..23 (0..7, ReverbIR::wave: per-column peak, 48 dB range) and playhead
  (0 hidden, 1..24): read-only, effCanBeAutomated false, not saved; wave in Inst atomics (worker writes), playhead
  from ReverbEngine::playhead() (note = block peak > -40 dBFS and > 2x recent level; crosses pre-delay + IR length),
  sent from notify() at most every 9 blocks. Device check: the T3K picture (same mechanism) updates on the Force.
- Page: art/texture.png under everything (frames are borders only), IR block left (RESCAN/DELETE in header),
  SHAPE block right (24 picture columns + playhead strip + START/END/FADE IN/FADE OUT/STRETCH/REVERSE), CONTROLS
  (MIX, PRE-DELAY, FEEDBACK, LOW CUT, HIGH CUT, WIDTH, OUTPUT). Art: tools/make_art.py (deterministic).
- NAME OPEN: user proposed "Parallax Echo", then "Parallax IR"; advised against (Keeley "Parallax" reverb/delay
  pedal; also Puremagnetik and Neural DSP plugins named Parallax). "Liminal Hz" is a placeholder; a rename is
  mechanical (see the earlier rename notes).

## 2026-10-02: renamed to "Liminal Hz"
MPC and catalog name "Liminal Hz" by Gm0rb; folder "Gm0rb - VST - Liminal Hz"; liminal_hz.so; vst/liminal_hz_vst.cpp;
catalog id liminal-hz (free); repo mpc-vst-liminal-hz; zips Liminal-Hz-for-MPC-OS-<v>.zip, Liminal-Hz-<v>-mpc-armv7.zip
(asset_pattern Liminal-Hz-*-mpc-armv7.zip); page tab / Q-Link bank "LIMINAL". State headers unchanged ("echo-vault=1",
and "ir-reverb=1" accepted). Name check: no "Liminal Hz" found; "Liminal" is used by FlickSwitch Audio (a transient
"dynamic threshold processor" plugin) and descriptively for Old Blood Noise's "Bathing" ("liminal delay"); judged
a much weaker conflict than "Parallax" (Keeley's reverb/delay pedal). No capital V (MPC's slot font lacks it).

## 2026-10-02: factory IRs, presets, Init, source (one page)
- tools/make_factory_irs.py -> packaging/factory/<Category>/<Name>.wav (14, stereo 24-bit 44.1 kHz, deterministic):
  early-reflection taps + a diffuse tail over 16 smooth log-frequency sub-bands, T60 interpolated from 4 anchors
  (125/500/2000/8000 Hz) -- the first version used 4 brick-wall bands and the spectrograms showed steps at 1 and
  4 kHz; fixed. Oddities: 120 Hz hum, beating tones + tremolo (Liminal Hz), brightening (Shimmer Fog), swell (Reverse
  Bloom), inharmonic modes (Glass Void). --check: clipping, DC, start click, decay/swell, L/R correlation.
- Params: source (50: Factory / My IRs), preset (51: Init + 14), preset__open (52, added by the layout's popup;
  closed by the wrapper on a pick; not saved, not automatable). Library::scan_folder (factory/ under the plugin
  folder, or MPCNAM_FACTORY for tests). No IR is auto-picked (fresh = Init). Stepping from "no IR": NEXT -> first.
  apply_preset: sets the plain params (set_real), source Factory, finds "<Category>/<Name>", loads; Init: defaults,
  no IR. DELETE refuses factory IRs. A TONE3000 download switches the source to My IRs before selecting it.
- Engine fix: set_ir(nullptr) now clears (clear_ flag: the audio thread crossfades the current IR out); before, it
  only dropped a pending IR (Init and failed loads kept the old IR playing).
- "Tail late" shows 5 s after the last late block (late_seen / late_until), not forever.
- Page (one page, per the user: no tabs): IR block: PRESET popup (cols=3: Init+Spaces | Echoes | Strange) and
  FACTORY/MY IRS at the top; name, pack, detail; buttons. A possible later addition, kept on the same page: a LIST
  button swapping the SHAPE area for a tappable list of My IRs.

## 2026-10-02: factory IRs, presets, Init, SOURCE (one page)
- tools/make_factory_irs.py: 14 synthetic stereo 24-bit 44.1 kHz IRs into packaging/factory/<Category>/<Name_With_
  Underscores>.wav (the wrapper shows underscores as spaces: factory_name/factory_file). Tail = 16 overlapping
  log-frequency sub-bands (raised-cosine, complementary) with T60 interpolated from 4 anchors (125/500/2k/8k Hz):
  the first version's 4 brick-wall bands made audible steps at 1 and 4 kHz (seen in spectrograms) -- fixed.
  --check-style checks at generation: no clipping/DC/start click, decay (Reverse Bloom: swell), decorrelation.
  Shipped by package.sh as factory/... in the plugin folder (not user data). ~14 MB.
- Library::scan_folder (this repo only) for factory/. Inst::libs[0] factory, [1] My IRs; P_SOURCE (50) picks.
  P_PRESET (51): Init + 14 (kPresets table in the wrapper, real values); P_PRESET_OPEN (52, the popup's flag,
  added by gen_vst from the layout's popup): set to 0 on a pick, not saved, not automatable. Init = defaults, no IR.
  No IR is auto-selected (fresh = Init); stepping from no IR: NEXT -> first, PREV -> last. DELETE refuses factory.
- Page (one page, per the user: no tabs): PRESET popup (cols=3: Init+Spaces | Echoes | Strange) + SOURCE in the IR
  block; the popup opens over the page and closes on a pick. MPCNAM_FACTORY env for tests.
- Can't audition audio here: the IRs are designed and checked objectively; the user judges the sound.

## 2026-10-02: design pass (design skills)
Consulted (cloned, used as guidance, not vendored): emilkowalski/skills (emil-design-eng), jakubkrehel/skills
(better-colors, better-layout, better-typography), jakubkrehel/make-interfaces-feel-better, MengTo/Skills
(no-ai-design-slop), codeswithroh/tastemaker, ConardLi/garden-skills. Applicable on an MPC page (no animation, live
text by MPC, art pre-rendered): removal test, group by space, one colour one meaning, type roles, one brand moment.
Changes: vst/liminal.css (art_css: hairline frames, quiet spaced titles, outline-free boxes, flatter buttons);
tools/make_art.py knob_strip() -> art/knob_big.png (160 px, 31 ticks) and knob_small.png (96 px, 11 ticks), 128
frames, used with strip=... frames=128 (big knobs r=54, small r=27); waveform bars bone (amber = live: arcs,
playhead); art/wordmark.png replaces the "CONTROLS" title; label_scale 1.0; vst/skin_post.py ROLES (live text size
and colour per definition + label name).

## 2026-10-02: layout rework, sonic SHAPE display, final background
- Layout (vst/layout.conf): asymmetric; browser column left (wordmark, PRESET, SOURCE, BROWSE TONE3000, stepper,
  pack/detail, PREV/NEXT PACK, DELETE, RESCAN), TONE3000 status lower left; SHAPE right: sonics readout, 16 wave
  pictures (wave2_<tone*8+level>.png), playhead (play2_0..16), ruler, captions trim/fade/time (art/cap_*.png);
  bottom: space (MIX r=57 hero, PRE-DELAY, FEEDBACK), tone (LOW CUT, HIGH CUT, WIDTH), out (OUTPUT).
- Sonics (engine/reverb_engine.cpp): per column level on a 66 dB range (48 left half the display empty) and tone
  (first-difference energy / energy; white noise 2.0; <0.22 dark, <0.75 balanced, else airy); describe():
  "decay X s" (capped at the IR's length: "decay X s+", echo patterns extrapolate past it) or "swells", colour,
  darkens/brightens, width, echoes/diffuse. P_SONICS readout. ReverbIR::kCols = 16.
- Background: tools/make_backgrounds.py threshold_air -> art/texture.png (doorway at 1092,452: lower right).
- Lessons: a leftover verification script from an earlier pass (rm -rf vst/build) collided with page builds:
  check `ps` before long jobs. Page images must stay under 16384 px tall: a 128-frame knob strip limits a knob to
  r <= 57 (2r+10 px per frame). plugin_test kCols constant must follow ReverbIR::kCols.

## 2026-10-02: layout rework, sonic shape display, Threshold background (verified)
- Background: tools/make_backgrounds.py threshold_air() (Deep Hall doorway glow + receding frames, floor light,
  haze, flow-field wisps, wavefront rings, dust) at (1092, 452): the empty lower right, light spilling down-left;
  copied to art/texture.png by the script. Other candidates kept in the script (threshold, deep, open, remix, chamber,
  cave, backrooms, great_hall, fluorescent, interference, poolrooms).
- Layout (vst/layout.conf): asymmetric; left column browser; right "shape" with the sonics line (P_SONICS readout),
  16 picture columns (wave2_<tone*8+level>.png) + playhead strip (play2_*) + ruler; captions as art (cap_*.png:
  trim / fade / time / space / tone / out); MIX r=60, PRE-DELAY/FEEDBACK r=42, tone/out r=36, shape r=26.
- Engine: ReverbIR::kCols 16; level on a 66 dB range (48 left half the display empty); tone per column from the
  difference-energy ratio (white noise 2.0: < 0.22 dark, < 0.75 warm, else airy); describe(): "decay X s" (or
  "decay X s+" when the estimate passes the IR's length), colour, darkens/brightens, width, echoes/diffuse.
- Found: a leftover verification job (/tmp/lh7.sh) from an earlier pass kept deleting vst/build mid-build (random
  missing-file failures) -- stopped. plugin_test still assumed 24 columns (54 params): updated to kCols = 16.
- All PASS: host, TSan, ASan, device, ARM (qemu), package; catalog zip 0 warnings.

## 2026-10-02: bigger UI, IR painting, travelling glow (verified)
- make_art.py v3: wave3_<tone*8+level>.png (44x160: jagged core trace + mist drawn past the edges, blurred, cropped;
  placed 42 apart so neighbours overlap 2 px -- the kit's rounding had left 1 px gaps), glow3_0..32.png (672x160:
  amber orb, trail, shimmer, motes), cap2_* captions (19 px).
- ReverbEngine::kPlay = 32 playhead positions (display keeps ReverbIR::kCols = 16 columns); playhead param 33 opts.
- Layout: larger browser (popup h42, stepper h58), shape painting at (564,146) 672x160 with the glow over it, shape
  knobs r32, controls Mix r58 / Pre-delay, Feedback r52 / tone, out r44 (moved up 18 px: Mix's value had reached
  the page edge). skin_post roles: name 36, preset 26, values 22, labels 15, pack 22, details 19, sonics 21.
- Mix r72 -> r58: the kit makes 128-frame filmstrips; at r72 that was 19712 px tall, over the 16384 px limit for
  one image (catalog_check warning; device textures). Frame ~ 2r+10 px, so r <= 58 keeps every knob under it.

## 2026-10-02: v4 shape (device-safe), larger again
User: the v3 shape graphics didn't load on the Force (bars v2 did). v3's two new things: a 33-state glow picture
(everything proven on the device had <= 25 states) and pictures overlapping by 2 px. v4 avoids both: ReverbIR::kCols
12 (56 px columns, edge to edge, mist drawn past the edges and cropped), ReverbEngine::kPlay 24 (25 states). No
sparkles; air = paler, translucent mist (spread 1.35, alpha 58), all mist fading towards top/bottom (no box edges).
Params 45 -> 41. Also suggested: re-insert the plugin after updating (a kept insert may hold the old param list).
Sizes: name 44, preset 30, values 26, labels 18, pack 24, details 21, sonics 23; Mix r58 (max: 16384 px filmstrip).

## 2026-10-02: why the shape pictures didn't show on the Force -- the real cause
(An earlier note here blamed "not automatable"; wrong: the bar version, which worked on the device, had the same
setting. Reverted.) The kit's switchable pictures are OPAQUE: each state image is the page background with the art
baked in, padded 2 px on every side (bounds = rect +-2). v3/v4 laid the glow picture over the whole painting, so its
opaque images covered it; and columns placed edge to edge overlapped each other by the 2 px padding (the "seams").
The bars worked because their playhead was a separate strip below them. v5: columns 52 px placed 56 apart (images
just touch), the glow as its own strip under the painting (glow5_*, 668x30). Mockups are now composed from the kit's
own state images at the kit's own bounds (TUI.json: Image components with IndexedEnabling/<state>/<n>/Parameter <i>),
not from the raw art: the raw-art overlays had hidden this.
Open: the user saw a pause when picking a preset (and after DELETE); not yet investigated.
Open: the "Tail late N×" count never resets (after one busy moment it stays on the detail line); should decay or reset per IR/preset.

## 2026-10-02: device confirmed the painting + glow strip work; text enlarged
User photo: the shape painting and the glow strip show and move. Text too small on the device: roles raised (name 48,
preset 36, values 32, knob names 24, pack 30, details 26, sonics 28, status 24); label_scale 1.0 -> 1.4 (the kit
sizes knob name/value boxes from it); readout boxes taller; bottom knobs up 10 px so value boxes stay on the page.
Second enlargement (user: "better but still could be larger"): roles name 52, preset 40, values 38, knob names 27
(max for the 116 px FADE IN / FADE OUT spacing with MPC's mixed-case parameter names), pack 34, details 30, sonics 32,
status 27, Reverse 25; label_scale 1.6; wordmark 380 px wide with "by Gm0rb" at 23 px. Note: the kit's preview draws
knob names from the layout labels (uppercase, wider); MPC uses the parameter names (mixed case).

## 2026-10-02: repo images and README
docs/images/: banner.png (Threshold: the README's top image; also set it as the GitHub social preview: repo
Settings -> General -> Social preview), page.png, shape.png, controls.png (each illustrates its README section),
page-render.png (input for page.png). All 1280x640, made by tools/make_banners.py from vst/art and real data
(regenerates them pixel-identical). README restructured for musicians first: banner, what it looks like, the shape
display, the controls, factory presets, install; then the CPU design, build and licence. Release README: the
"The page" section's PRESET / FACTORY bullets now point to "Presets and factory IRs" (was duplicated).
