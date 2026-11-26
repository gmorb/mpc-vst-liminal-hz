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
(Was open: the pause on a preset pick / after DELETE -- see 1.4.0 below. The "Tail late N×" count: fixed, shown 5 s after the last late block.)

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

## Gen 2 (aarch64, 1.2.0; tested on an MPC Live III, MPC OS 3.9.1, 2026-10-12)
- One plugin, two builds (mpc-vst-plugins docs/GEN2.md): `vst/build.sh` (Gen 1, armhf) and `vst/build.sh gen2`
  (aarch64, Zig `aarch64-linux-gnu.2.36 -mcpu=cortex_a55`: ARMv8.2-A features both RK3588 core types have). Same uid
  `NmIR`, params, presets, page. `tools/package.sh` makes `Liminal-Hz-<v>-mpc-aarch64.zip` when the Gen 2 .so exists.
- Everything that differs is in `engine/platform.h` (`LIMINAL_GEN2`): kMaxSeconds 20, kFarBlock 16384 (third
  convolution stage, its own worker), sinc IR resampling, Decay STFT 4096 (hop 512 kept), PFFFT NEON
  (`-DPFFFT_ENABLE_NEON`, Gen 2 build only: the Gen 1 build has always compiled PFFFT scalar, because the flag was
  never set; enabling it there would be a free speed-up but needs a Force re-bench first).
- LongConvolver: `stages_` (each Stage: block W, taps [2W, next stage's 2W or N), in_ring 4W, out[3], own sem and
  thread). Gen 1 constructs exactly one stage, as before. `LongConvolver(ir, mode, far)` lets tests pick the far block.
- Memory on Gen 2 for a 20 s stereo IR: about 28 MB of partition spectra + input FDLs per IR (twice during a
  crossfade); Decay's STFT needs ~56 MB briefly while preparing.
- Tests: `GEN2=1 tools/test.sh` and `AARCH64=1` (qemu-aarch64; needs libc6-arm64-cross). Device: 1.2.0
  tested by the user on an MPC Live III (Gen 2) and an Akai Force (Gen 1), both MPC OS 3.9.1, 2026-10-12 (tested.json).
  Not yet measured there: `bench_reverb` built for aarch64 (worker load and misses at 20 s stereo).

## LONG IR, and Gen 2's engine on Gen 1 (1.3.0; offline only so far)
- `engine/platform.h`: `Quality` {max_seconds, sinc, decay_fft} is a runtime setting. standard = 5 s, cubic, 2048;
  high = 20 s, sinc, 4096. Gen 1: standard unless the LONG IR parameter is on; Gen 2 (`LIMINAL_GEN2`): always high.
  `ReverbIR::make/load` take the Quality; `Browser::loaded_quality` makes a change prepare the IR again (crossfaded in).
- Both builds now always have the far convolution stage (`platform::kFarBlock` 16384) and PFFFT NEON
  (`-DPFFFT_ENABLE_NEON` in vst/build.sh's armv7 target too). Before 1.3.0 the Gen 1 build ran PFFFT scalar.
- Parameters appended after `decay`: `long_ir` (Off/On, saved in the project chunk, not in presets, untouched by Init
  and factory presets, not automatable), `long_ir_tap` (momentary: off -> arms for kArmSeconds (4 s), a second tap in
  that time -> on; on -> off at once; Gen 2: no effect), `long_ir_info` (readout: "Off · IRs up to 5 s", "CPU heavy ·
  tap again", "On · IRs up to 20 s", "Gen 2 · always on"). `P_PRESET_OPEN` (the popup's flag) moves 3 indices later:
  projects store parameters by key, so nothing changes for them.
- Page: `toggle cx=1160 cy=608 "LONG IR"` and `readout cx=1160 cy=660 w=236` under REVERSE (skin_post role
  shReadout_236x34). No new skin_check warnings (the 19 there are the lists laid over the page, as before).
- To do on a Force: `tools/bench_reverb` built for armv7 (2-stage vs 3-stage worker load, 5 s and 20 s stereo, misses),
  MPC's CPU meter with LONG IR off and on, and how long a 20 s IR at Decay 300% takes to prepare.
- Resampling cache (engine/reverb_engine.cpp `ResampleCache`): one entry, keyed by an FNV-1a hash of the channels plus
  rate, target rate and quality; a shape change re-prepares from it. PC: 20 s 48 kHz stereo at high quality, first
  load ~1.05 s, End/fade/Reverse ~0.12 s, Decay 150% ~0.57 s, Stretch (resamples) ~1.0 s. PC bench (worker, % of a core,
  stereo): 5 s 1.9 -> 0.7, 10 s 3.3 -> 0.8, 20 s 7.0 -> 1.3 (2048-only vs with the far stage).

## 1.4.0: the preset pause, Duck, SYNC, Q-Links, Textures (2026-11-08; offline only so far)
- **The pause** (open since 2026-10-02). Measured offline (PC, a paced host with a UI thread that re-reads every
  parameter on each UpdateDisplay, as MPC rebuilds its screen): the plugin itself was cheap (setParameter on a pick
  20-50 µs, a factory IR prepared in 7-25 ms, no audio blocks late), but a pick sent 2 UpdateDisplays (the pick, then
  the IR ready) + 31 Automates, and every knob step sent an UpdateDisplay. MPC answers each UpdateDisplay by reading
  every parameter and rebuilding the screen (mpc-vst-plugins wrapper/vst2_wrap.c), so the screen work was the likely
  pause. notify() now paces them: at most one per kDispGapMs (100 ms), held up to kDispHoldMs (600 ms: a factory IR is ready in ~30 ms on a PC, ~400 ms under qemu-arm) while
  Browser::loading (read with try_lock), and an Automate is skipped when an UpdateDisplay is due and the host already
  has the value (host_view[], from setParameter and Automate; reset to -1 on open and setChunk). A pick: 1 + 18.
  DELETE used to rescan the cards (walk -> "Searching..."); now the worker drops the entry from libs[1] and selects
  the same index (the next IR), as the rescan did. ResampleCache: 4 entries, LRU, each capped at the quality's
  max_seconds (<= 7 MB). **Device check:** a preset pick and a DELETE on the Force should no longer hitch; if they
  still do, time the screen redraw (the pick's single UpdateDisplay) and the Automates.
- **Duck** (`duck`, 0-100%, appended): ReverbEngine, in the output loop after the feedback tap (fbl_/fbr_ are taken
  before it): a peak follower on the input (kDuckFollow 20 ms), reduction target kDuckDb (24) x clamp((dB - kDuckLoDb
  -50) / 30), smoothed (kDuckAttack 5 ms, kDuckRelease 80 ms), gain exp(-ln10/20 x red x knob), the knob glides per
  block. Duck 0: the follower is skipped and reset, so the output is bit-identical.
- **SYNC** (`predelay_sync`, Off/On, appended): the wrapper's kNotes (11 steps, beats); process_replacing() asks
  audioMasterGetTime(kVstTempoValid) every block while SYNC is on (kit NOTES.md: MPC answers flags 0x7fc4 with a valid
  tempo, verified on Gen 1 2026-09-23; Gen 2 assumed the same host code), sets predelay_ms = beats x 60000 / bpm. No
  tempo: 120 BPM, and the value reads "1/8 @120". kMaxPredelay 250 -> 2000 ms (2 x 88 k floats).
- Both appended before P_PRESET_OPEN; kPresetParams has them; mp_apply sets both off before applying (older .lhzp);
  apply_preset sets both 0; Init resets them (defaults).
- Page: bottom row respaced (MIX 84, PRE-DELAY 208, SYNC toggle 300 bw=72, FEEDBACK 392, LOW CUT 512, HIGH CUT 628,
  WIDTH 744, DUCK 860, OUTPUT 976; captions tone x=480, out x=826). New skin_check notes: knob boxes overlap 14 px
  (as the trim row always did), FEEDBACK/LOW CUT 10 px, SYNC 9 px each side. SYNC was bw=60 at first, and MPC cut
  the word on the device: its label font is 25 px ("SYNC" ~58 px wide), and a box wants ~20% more than the text.
- Q-Links (both banks): mix, decay, feedback, predelay, duck, lowcut, highcut, width, ir, stretch, start, length,
  fade_in, fade_out, reverse, output.
- Textures: tools/make_factory_irs.py appends 5 builders (seeds continue: the 14 earlier files are byte-identical);
  packaging/factory/Textures (3.2 MB); 5 presets appended (PRESET: Init + 19; norm = k / 19); the popup is cols=4
  cw=300 (it was 3 x 340, which a fourth column pushed off the page). tools/demos/demo_host divides by 19.
- **MPC's preset menu** (added the same day): VST programs = Init + kPresets, named program_name() "Space|Echo|Strange|
  Texture: <name>" (<= 23 chars, VST's 24-byte buffer); effSetProgram -> set_parameter(P_PRESET), then host_view[P_PRESET]
  = -1 and changed (the host didn't write it); re-selecting the current program is ignored (JUCE re-selects at load;
  the kit's rule, mpc-vst-plugins NOTES.md 2026-10-07). effGetProgram is the PRESET param's index (after a My Preset
  it shows whatever PRESET holds). **Device check:** the menu lists 20 names, picking one loads it and our PRESET field
  follows; a project reload keeps edits.
- **Best on**: kBestOn (factory IR name -> "drums, vocals"...), appended to the pack line for the Factory list
  ("Strange · best on chords, pads"); My IRs and TONE3000 packs unchanged. README has the table.
- **Marketing kit**: tools/demos (12 demos; make_video.py takes several wet parts for before/after, demo_host takes
  bpm=), tools/trailer (score.py: two songs, the style setting the tempo and meter: dark, 120 BPM in
  5/4, a round sub ostinato (sine + triangle, under ~700 Hz), heartbeat, a glass pad, a theremin-like falling lead,
  toms; cyber, 100 BPM, slow and heavy: a broken kick, a backbeat snare, an 8th bass pulse, 8th-gated chords, a soft
  square hook; trap, 140 BPM half-time: sliding 808s, a clap on 3, hat rolls, a dark mono synth through Dream
  Ping-Pong. render.sh <style> plays every stem (none dry) through a factory preset, mix.py <style>
  cuts the tails that would ring over a hit (GATES) and masters, make_trailer.py wide|tall --style <style> draws the
  dark edit (paintings, red titles), cyber's own edit in the plugin's amber and ivory (a terminal boot, HUD
  panels, a scope, a countdown, punch-ins) or trap's (hard cuts on the 808s and claps, huge stacked words, a ghost
  word drifting behind, page punch-ins),
  from the real page states), and tools/make_feature_images.py (docs/images/feature-*.png, plus 1080x1080 posts). The trailer files are not in the repo
  (~40 MB): upload them (YouTube, socials) and link them.


## 1.4.0 after the first device test (2026-11-11; offline, to check on the device)
- **LONG IR**: the user saw a tap warn and turn it on at once, and the warning again on the way off: each tap
  evidently reached `long_ir_tap` twice (arm, then confirm). The page control is now `toggle key=long_ir` (the On/Off
  setting itself, exactly like REVERSE); the info line reads "On · CPU heavy · 20 s" while on. **Not** `button`:
  the kit's button (`shTrig_*`) has a Mouse Down "Toggle Switch" action *and* a Button component that both flip the
  value, so a tap flipped it twice and LONG IR never stayed on (the second device report). A `toggle` (`shToggle`)
  routes Mouse Down to Q-Link focus and lets the pill alone switch it. MPC writes the parameter name ("Long IR") under it.
  `long_ir_tap` stays (appended params never go) as a toggle that counts events within `kTapMs` (300 ms) as one.
- **DELETE** (IR and My Preset) keeps two taps; the confirming one must come >= kTapMs after the arming one, so a
  doubled event only arms (plugin_test sends doubled taps). Check on the device that a real double tap still deletes.
- **SYNC's label**: MPC writes the parameter's *name* under a toggle (Label "type": "Name"), not the layout's label,
  so it showed "Pre-delay Sync" in a 72 px box. params.json's name is now "Sync". (The page preview shows the layout
  label, which is why the earlier 60 -> 72 px fix looked fine offline.)
- **MPC's preset menu (VST programs)**: `kProgOrder` lists the factory presets by use (Drum, Vocal, Keys, Pad, FX;
  "Use: Name", <= 23 bytes); program 0 is Init; then up to `kMaxProgIrs` (500) My IRs read with
  `Library::scan_quick` at instantiation (the folder index the last scan left: no card walk; a first-ever load has none),
  named "My IR: <file>". `numPrograms` is fixed per instance, so new IRs show on the next load. A My IR program =
  Init's settings + that IR (`pick_ir_program`; found after the scan if needed). `cur_prog` is what effGetProgram
  reports (the project's My IR or PRESET after a chunk). effSetProgram is ignored for the current program and for
  2 s after effSetChunk: My IRs' places move, so a host re-selecting its saved number could name another sound.
  **Device check**: that MPC's menu lists the My IRs, that picking one loads it, and what MPC does at project load.
- **Back to Init**: any IR picked from My IRs (arrows, packs, IR List, a TONE3000 download, the menu) resets the
  settings to Init (`ir_picked` -> `back_to_init`; the user asked for this on My IR -> My IR switches too, after a
  first version only did it after an untouched factory preset). Not when the IR didn't change (`before == want`: a host
  re-sending P_IR), nor within 2 s of effSetChunk (a host restoring P_IR with the project). My Presets set the IR in
  `mp_apply` and never come here. `preset_clean` is still saved in the chunk but no longer gates anything.

## 1.4.0, re-tagged: one tap, one step (issue #5; 2026-11-12; offline, to check on the device)
- Report (ekeppel, MPC Key 61, MPC OS 3.10): NEXT/PREV PACK and the lists' PREV/NEXT stepped on press *and* release.
- Cause, in the kit's skin: every `button` (shTrig_*) (and list row, shRow_*) had a Mouse Down action "Toggle Switch"
  *and* a Button component (gestureBehaviour Instant) that switches the value again. The plugin reads these back as 0
  (get_parameter), so both writes arrived as 0 -> 1 presses. The same thing doubled LONG IR's old tap parameter and
  DELETE's confirming tap (the kTapMs guards in 1.4.0 worked around it for those two).
- Fix: `vst/skin_post.py` sends Mouse Down to "Q-Link" on the buttons, exactly as `shToggle` (REVERSE, LONG IR)
  does with the identical Button component, which the user confirmed switches once per tap on a Force and a Live III.
  Enter on the encoder keeps "Toggle Switch". skin_post fails the build if any other definition with a Button (other
  than a row) still switches on Mouse Down, or if none was found (the kit changed).
- **Not the rows.** A first build changed the rows too, and on the device a tap on an IR List or My Presets row then
  did nothing (the user, 2026-11-12). Why a row's Button alone doesn't write, when a toggle's does, isn't known (the
  row's value reads back 1 for the item in use, a button's always 0; maybe that). The rows keep the kit's Mouse Down
  "Toggle Switch"; picking a row twice is harmless (same IR: before == want, no Init; same My Preset: applied again). No plugin code changed; kTapMs stays as a guard.
- To check on a device: NEXT PACK steps one pack per tap; the IR List's NEXT one page per tap; a row tap picks an IR
  or a My Preset; CLOSE, DELETE
  (two taps: arm, confirm), SAVE, RESCAN, BROWSE still work; holding a button doesn't repeat.
- Also: the Breath sound-design demo is an old music box (liminal-hz-demo-musicbox.*), replacing the hum.
- Released as 1.4.0 (the user's call): the first v1.4.0 tag (6e1342a) was never published, so it was deleted and
  v1.4.0 re-tagged on the merge of this fix.

## 1.4.1: MPC OS 3.10.1's official plugin folder (2026-11-12; the user confirmed the converted build on the device)
- MPC OS 3.10.1 (Akai's MPC Plugin SDK, inmusic-software/mpc-plugin-builder-sdk) loads plugins from
  `<drive>/VST2 Plugins/<Manufacturer> - VST - <Name>/` once Preferences → Enhancements (Beta) → Enable Enhancements +
  External Plugin Support are on. The folder needs `plugin.json` (name, manufacturer, uid, file32/file64) and
  `version.xml` with `<uiversion>0.1.0.0</uiversion>` (without it the MPC may ignore the Plugin Skins).
- `tools/package_official.py` (called by package.sh when both device zips exist) builds
  `Liminal-Hz-<version>-MPC-OS-3.10.1.zip` from the two community zips' portable folders: the same binaries, renamed
  `liminal-hz-arm32.so` / `liminal-hz-arm64.so`, at the folder root beside factory/, presets/, art/ (the plugin finds
  its data next to its own .so: Library::plugin_dir). The reverbs/README's IR-length line is merged for both
  generations; the old installer's plugin-meta.xml and mpc-plugin.json are left out. CI uploads it with the other zips
  (dist/*.zip).
- The SDK's `tools/validate.py` on the folder: 0 errors, 0 warnings. Its `host_sim test` on the host build: 20 passed
  (it plays the MPC's quirks, including the parameter-0 toggle on every prepare: Mix, harmless). It first failed one
  check: a project's state left an open list open; set_chunk now closes it (menu = 0, nothing armed).
- Not verified: whether projects saved with the old (Synths / install.sh) install find the new one.

## The user manual (2026-11-13)
- `docs/Liminal-Hz-<major.minor>-User-Manual.pdf`, built by `tools/make_manual.py` from `docs/manual-src/manual.html` +
  `manual.css` (Chromium via Python Playwright, then pypdf puts the footer-less cover in front). Filled in at build time:
  VERSION, the factory preset table (parsed from kPresets), the page-overview callouts (CALLOUTS: page coordinates,
  layout.conf y - 86), the signal-flow SVG, the contents page numbers (a first print finds each "CHAPTER n").
- Fonts from the kit (MPC_VST/tools/html_art/fonts), images from docs/images (page-render, ir-list, my-presets, shape,
  phone) and vst/art/texture.png for the cover. Rebuild after any change to the page, presets or controls.
- 1.3's manual was only a release asset (Liminal-Hz-1.3-User-Manual.pdf, no source in the repo); 1.4's text is that
  manual updated. package.sh copies the PDF into dist/ and the release workflow uploads dist/*.pdf.

## 1.5.0: the on-screen keyboard (2026-11-17; offline, to check on the device)
- Asked for (the user): MPC OS 3.10.1 has a keyboard for its SSH terminal; can presets be renamed on the screen?
  MPC's keyboard can't be reached from a plugin's skin: a skin has no text component, the known overlays are
  "knob overlay" (a number), "menu overlay" (empty for VST2) and "numeric overlay" (a stuck black panel), and an
  overlay hands the plugin a 0..1 value, never text (mpc-vst-plugins docs/NOTES.md). So the keyboard is the plugin's own.
- Layout (`vst/layout.conf`): a `kb` option parameter (Off/On, set by the plugin). Every control on the right-hand side
  is `when=kb:0`; the keyboard is `when=kb:1`: `art/kb_panel.png` (708x620 at x=566 y=92) first, so its mode image
  is drawn after the IR painting's and the glow's (the kit's pictures are opaque images: the panel covers them, the
  same mechanism that hid the v3 shape behind its glow), then the title, the name (readouts 664 wide) and five `list`
  widgets: 4x10 keys (61x74) and CLEAR / SPACE / CANCEL / SAVE (118, 268, 118, 142 wide), drawn from
  `art/key_*.png` (`tools/make_keyboard_art.py`; lit = amber). Text sizes in `vst/skin_post.py`.
- Keys are list tiles because a tile's text is live (the letters follow SHIFT) and a tile is the one control whose tap
  is a single write: its Value label takes the touch, so only the Mouse Down "Toggle Switch" writes (the Button inside
  never sees it: which is also why "Q-Link" on rows did nothing in issue #5). Plugin (`set_kb_param`): a write is a
  tap unless it is 0 while the key is lit (the second half of a doubled touch) or within 50 ms of the last tap of that
  key; MPC's write-back of an Automate (from inside the call) is ignored by index (`echo_i`). A tapped key reads 1
  for 160 ms (lit), then notify() reports 0 with audioMasterAutomate (MPC doesn't re-read a button on UpdateDisplay).
  SHIFT reads 1 while it's on.
- Parameters: `preset__open` is now listed in params.json at index 64 (where the kit appended it), then mp_rename,
  kb, kb_title, kb_name, kb_info, kb_1..kb_40, kbc_1, kbs_1, kbx_1, kbok_1 (114). No index moved. None of the keyboard
  is in the chunk; a project load closes it. The old one-tap SAVE (unique name, worker `save_req`) is gone: SAVE opens
  the keyboard with `kb_suggest()` (the IR's name, numbered among the listed presets), its SAVE goes to the worker
  (`Keyboard::req`), which closes the keyboard on success or keeps it open with the reason.
- Tests: plugin_test's keyboard block (typing, capitals, SHIFT cycle, DEL/CLEAR/SPACE, 32 characters, a doubled touch
  types once, MPC's write-back types nothing, the flash goes dark and is reported, SAVE / replace / RENAME / refusal /
  CANCEL, the list waits, CLOSE, projects). All tests pass, also under ASan and TSan. (Once, in one of six runs, the
  SYNC sample-count check was off by ~1200 samples under load; it passed on the reruns.)
- Docs: README "The keyboard", manual 1.5 (chapter 8; `docs/Liminal-Hz-1.5-User-Manual.pdf` replaces 1.4's),
  docs/images/my-presets.png (now with RENAME) and keyboard.png: page renders (tools/demos/render_page.py) of states
  driven through the real plugin.
- To check on the device (Force, Live III): a key tap types once and the key flashes; SHIFT; SAVE / RENAME / CANCEL;
  the right-hand side comes back after; nothing of the waveform shows through the panel; typing feels quick enough
  (each tap asks for one UpdateDisplay, paced at 100 ms).

## 1.6.0: FREEZE and END SYNC (2026-11-18; offline, to check on the device)
- Asked for (the user, from a list of ideas that keep to convolution and a quiet page): Freeze and a tempo-synced tail.
- FREEZE (`engine/freeze.h`, header-only, audio thread, allocation-free after construction): the wet signal as heard
  (pre-filter: the reverb, or a fading pad) goes into a 16384-sample ring while not frozen. Pressing it copies the
  ring and analyses it over the next 6 blocks (one 8192-point FFT a block: 3 half-overlapped Hann windows x 2
  channels, power averaged); the pad is that magnitude spectrum with random phases (LCG, a 4096-entry sin/cos table),
  inverse FFT, Hann, overlap-added at hop 2048, scaled so its rms is the ring's last 4096 samples' (Parseval: every
  frame has the same energy, so the gain is set from the first). The next frame is made one channel per block ahead
  of each hop: ~2 inverse FFTs per 46 ms while it plays, nothing when it doesn't. In ReverbEngine::block: the
  convolution's input (and the feedback loop) is muted over kInMute; the pad comes in over kFreezeIn (equal power)
  as the reverb goes out; let go, the reverb is back in kLiveBack and the pad fades over kFreezeOut, then stop()
  ends the frame making. The pad goes through the filters, width, Duck, mix and output like the reverb. The playhead
  hides while frozen. Engine tests: level held (-0.1 dB, flat over 10 s), input ignored, fade out, refreeze while
  fading, silence, pitch kept (a 440 Hz note: 100% of the pad's energy at 400-480 Hz), stereo (L/R corr ~0).
- FREEZE is a switch, not a setting (`is_setting` false): not in presets, not in the chunk, and set_chunk turns it off.
- END SYNC (`length_sync`, a setting, saved like predelay_sync): `IRShape::length_s` (> 0: keep that many seconds
  from Start instead of the End share; longer than the IR: all of it). The plugin's `shape_of()` sets it from
  `kEndNotes[end_note()]` at the tempo, rounded to 1 ms; the worker's load_ir() runs every 250 ms and compares shapes,
  so a tempo change prepares the IR again by itself. The tempo is read when SYNC or END SYNC is on. End shows the
  note ("1/4", "2 bars", "+ @120" without a tempo from the host).
- Layout: the lower-right corner is a 2x2 of toggles (bw=100): FREEZE (1092,536), REVERSE (1204,536), END SYNC
  (1092,608), LONG IR (1204,608, moved from 1160); LONG IR's line stays centred under them (1160,660).
- Params 116 (length_sync 114, freeze 115, appended). Plugin tests: END SYNC ends a click's reverb at 250 ms
  (120 BPM, 1/8) and 500 ms (60 BPM) plus the filters' ring-out, display, project, factory preset; FREEZE through the
  plugin (held -0.3 dB, flat), presets leave it, projects don't keep it, fades out.
- To check on the device: FREEZE on a held chord sounds smooth (no "underwater" swirl worth worrying about), the CPU
  line stays fine on Gen 1 with it on, END SYNC gates in time and follows a tempo change.

## 1.7.0: SHAPE / ROOM tabs, Distance and Damping (2026-11-18; offline, to check on the device)
- From the list of ideas (after Freeze and END SYNC): Damping and Distance, with room made for them by tabs.
- Engine (`reverb_engine.cpp`, in ReverbIR::make after Decay and the cap, before Reverse and the trim, i.e. in the
  IR's own time): `damp_ir()` runs two one-pole low-passes whose cutoff falls from 18 kHz by kDampOct (4) octaves a
  second at 100% (amount^1.3, floor 120 Hz): O(n). `distance_ir()` finds the onset (first sample at a tenth of the
  peak), splits early (80 ms) and tail with a 20 ms raised-cosine crossfade: near up to +6 / -6 dB, far -14 dB on the
  early part plus a one-pole on it (18 kHz down to ~3.2 kHz). The unit-energy step after keeps the level. Damping 0
  and Distance 0.5 skip both (bit-identical to 1.6: tested). Engine tests: highs of the tail at 1-1.5 s -20 dB at
  100% (start within 1 dB), early/tail balance +12 dB near / -20 dB far, far's early part darker.
- Tabs: `tab` (Shape / Room) is the enum_h at the top right (where the "shape" caption was; the sonics readout is
  480 wide now: `shReadout_480x42` in skin_post). The knob row's knobs and captions are `when=rpage:<n>`; `rpage`
  (Shape / Room / Keys) is read-only and is the tab, or Keys while the keyboard is open (a widget takes one `when`,
  and the row must follow both). Neither is a setting or in the chunk.
- `damping` (116), `distance` (117) are settings (kPresetParams, the chunk); factory presets and Init set 0 / 50.
  `tab` 118, `rpage` 119 (120 params). Q-Links unchanged (16 taken): Distance and Damping have none.
- `cap2_room.png` drawn like the other captions (`tools/make_art.py` lists "room" now; the file was made with the
  same font and colour without re-running the whole script).
- To check on the device: the tabs switch the row at once; Distance and Damping are audible and quick enough to
  prepare on Gen 1 (O(n); Decay's STFT is still the slow one); the keyboard still covers the row on both tabs.

## 1.8.0: SCAN (2026-11-19; offline, to check on the device)
- The user's idea: an XY effect on the painting to mangle the sound by touch. MPC plugin skins have no XY pad (no
  XYPad component in any stock skin; a touch goes to one control; a drag is a stream of nudges of one value:
  mpc-vst-plugins docs/NOTES.md), and re-preparing the IR per drag would stall the device. So: a grain scanner over
  the reverb's recent sound, one axis by dragging the painting (Position), the other by a knob (Texture).
- Engine `scan.h` (header-only, audio thread): `GrainScan::write()` every block puts what's heard (after FREEZE's mix,
  before the filters) in a 6 s ring. `render()` while on: grains spawned every glen / overlap (jittered),
  glen = 12 ms + 340 ms x (1 - tex)^2, overlap 3..8; each reads from 30 ms + position x depth (depth = the IR's length,
  so the glow over the painting points at the matching moment of the tail) + its own length x ratio back, never
  reaching the write head; Hann from a 1024 table, linear interpolation, up to 16 voices; pitch: none below 1/3,
  octaves and fifths up to 70% of grains in the middle, continuous +-1.25 octaves above 2/3 with some reversed;
  stereo spread (a grain to a random pan, the sides summed at -3 dB: the reverb is wide) grows with texture. Level:
  1.23 / sqrt(overlap x 0.375) (the 1.23 measured: within 0.3 dB at textures 0, 50, 100%). In ReverbEngine::block:
  after FREEZE, `sc_on_` ramps over kScanFade (60 ms); the reverb x sqrt(1 - a^2), grains x a (a = sin x Amount).
  The playhead shows the position while scanning. Off: bit-identical (tested).
- CPU (PC, Liminal Hz IR): reverb 0.7% of real time, + scan 0.9% (texture 0) / 1.1% (texture 100). Gen 1 to check.
- Layout: tabs `tab` Shape / Room / Scan, `rpage` + Scan (Keys is 3 now); the tabs are 3 x 88 at cx 704, the sonics
  readout 400 wide (27, `shReadout_400x42`). SCAN page: toggle SCAN, knobs POSITION, TEXTURE, AMOUNT, and
  `slider_h cx=906 cy=212 w=668 h=148 key=scan_pos ns=0 when=rpage:2` over the painting: `skin_post.py` makes its film
  strip clear and drops its labels (it fails the build if it can't find it).
- Params 124: scan 120 (a switch: not a setting, not in the chunk, off on project load), scan_pos 121, scan_tex 122,
  scan_mix 123 (settings; factory presets and Init: 0, 0, 100).
- To check on the device: dragging across the painting moves Position (and the glow) smoothly enough, with no
  focus box or other drawing over the painting; SCAN's sound at a few textures; CPU on Gen 1 with SCAN, FREEZE and
  LONG IR together.

### 1.8.0 test build 2 (2026-11-19): SCAN rebuilt, tabs fixed (after the user's device test)
- The user: SCAN "doesn't sound good", Texture "pixelated or low bitrate"; and the tabs sometimes lit one tab and
  showed another page's knobs (or switched the lit tab without the knobs).
- SCAN is now `SpectralScan` (scan.h): FREEZE's method, which the user liked. Every hop (2048) a Hann window of 8192
  from the ring at the position (back = 20 ms + position x depth + a window) -> magnitudes smoothed over time
  (0.35 at texture 0 to 0.98 per hop), random phases, overlap-added, each frame scaled to the (smoothed) rms of what
  it read. Texture: smear (the smoothing) up to 1/3; shimmer (+ 1.2 x the octave up, 0.8 x the fifth up from below
  5 kHz with a roll-off, 0.35 x the octave down) peaking at 2/3; wind above: a box blur of +-12% of the frequency
  (constant Q: no rumble) blended in, and 24 log bands drifting +-9 dB (random walks, ~0.3 Hz). Per hop 2 forward +
  2 inverse FFTs, one per block (the first frame is made over 4 blocks: no spike when SCAN comes on; the first hop is
  silent under the 60 ms fade). PC: reverb 0.8% of real time, + scan 1.4-1.6%. The grain version (12 ms grains with
  pitch jumps, linear interpolation) is gone: that was the buzzy, bit-crushed sound.
- Tabs: the enum_h (MPC's radio buttons hold the host's value; the knob row followed the plugin's rpage, so the two
  could disagree) is replaced by a 1x3 `list` of tiles (`tabs_1..3`, tab_88 art), lit by the plugin as the keyboard's
  keys; any write to a tile picks it (MPC's write-back of a reported light is ignored by index, echo_i). Each page's
  widgets follow its own two-option switch, `pg_shape` / `pg_room` / `pg_scan` (as `kb`); `rpage` is gone. `tab` stays
  as the plugin's state (0..2), not in the skin. Tabs and pages are reported to the host every time (as the keys).
- Params 129: ... damping 116, distance 117, tab 118, pg_shape 119, pg_room 120, pg_scan 121, tabs_1..3 122-124,
  scan 125, scan_pos 126, scan_tex 127, scan_mix 128 (all after 1.4.1's 65 unreleased, so renumbering was free).
- A listening demo was sent before the build (chords into Backrooms: reverb, then Texture 15/50/90% with a slow
  Position sweep).

### 1.8.0 test build 3 (2026-11-19): SCAN's GRAINS mode and PITCH
- The user, on test build 2: SCAN "sounds better but missing more sound manipulation"; asked for grains and pitch.
- Three grain demos were rejected for "pops and clicks", "jittery", "chiptuney". Findings, in order:
  (1) random grain starts cancel: on a steady tone the level swung 26-36 dB (fixed: WSOLA alignment of each new grain
  to the playing one by normalised cross-correlation, coarse every 4 samples then +-3; even spacing; the sum divided by
  the windows' sum when they overlap; a slow level match, 0.5-2.5x); (2) grains of the reverb's output had nothing to
  smooth their edges: GRAINS now granulates the pre-delayed INPUT and sends the grains into the convolution (the IR is
  the diffuser, as Clouds' diffuser); (3) the pops the user still heard were in the plain reverb pass too: the demo
  host ran faster than real time and LongConvolver's tail worker missed 2-21 deadlines per run (misses() counts them).
  Paced at real time (sleep 23 ms per 8 blocks): 0 misses in every run. Always pace demo renders.
  (4) "chiptuney": the demos' additive sine chords; a warm detuned-saw pad sounded "ok".
- Licences checked: Argotlunar and BORIS Granular Station are GPL (ideas only, no code); gRainbow's upload was
  binaries. Mutable Instruments' Clouds (MIT) was the model for "smooth the grains after".
- Params 136: scan_mode 129 (named "Grains", Off/On: the MPC shows a switch by its parameter name, so "Scan Mode" showed in test 3; a setting), scan_pitch 130 (-24..24 st, real_of rounds), grain_size 131
  (30-500 ms, is_log), grain_spray 132, grain_density 133 (overlap 0.5 x 12^d), pg_scan_pad 134, pg_scan_grains 135
  (page switches: pg_scan and the mode; read-only, not saved, told every time as the other pages).
- Engine: ReverbParams scan_mode, scan_pitch, grain_size (0..1 log), grain_spray, grain_density. GRAINS runs right after
  the pre-delay (gc_on_ fades 60 ms, equal power with the input); PAD stays after the convolution. PAD's pitch reads the
  held magnitudes at k / ratio. GrainCloud: 24 voices, Hann table, Catmull-Rom, two one-poles at 0.45 x rate / ratio
  when shifted up. PC cost, worst case (30 ms grains, density 100%): ~1.8% of a core; typical well under 1%.
- Layout: SCAN and GRAINS toggles stacked at x 598; PAD: POSITION 714, TEXTURE 846; GRAINS: SIZE 714, SPRAY 846,
  DENSITY 1092; both: PITCH 962, AMOUNT 1204. Factory presets, Init and presets saved before this reset them (PAD, 0 st,
  150 ms, 20%, 60%).

### 1.8.0 test build 4 (2026-11-25): GRAINS' DIRECT
- The user, on test build 3/3b: "working good", likes the sound; asked how to get a more granular effect (taught:
  low Density, short or long Size, Spray, Position far back, Amount 100%, more wet, a short IR or less Decay), then
  asked for the proposed control. (3b only renamed scan_mode's parameter "Grains", Off/On: the MPC labels a switch
  by its parameter name, not the layout's label.)
- grain_direct 136 (0-100%, default 0 = test 3's sound). Engine: the grains are split equal-power, cos to the IR and
  sin x kDirectGain (1.12, +1 dB: measured on factory IRs with noise, the grains through the IR were 0.2-2.7 dB above
  direct) into gd_l_/gd_r_, added to the wet after the convolution and FREEZE, before
  SCAN PAD's ring write. Test: 50% / 100% keep the level (+0.7 / +1.1 dB); at 100% the 10 ms level swings 30 dB (the gaps)
  vs 2 dB through the IR.
- Layout: the SCAN row is six slots 102 apart (692 .. 1202): PAD POSITION, TEXTURE; GRAINS SIZE, SPRAY, DENSITY,
  DIRECT; both PITCH 1100, AMOUNT 1202. Touch areas of neighbours overlap 28 px (the kit warns; the other rows 14).

### 1.8.0 test build 5 (2026-11-25): AIR, gliding PITCH, SNAP, the switch tiles, grain streaks
- The user: test 4 "working good"; asked for lusher, "airy" grains like high-end granular reverbs; liked AIR's demos
  (warm pad, then their DAYBREAK.WAV: "sounding ok"); sent a GRN (FRCTL) walkthrough video: wants the waveform to show
  what the granulator does (GRN draws orange grain lines over it) and PITCH smooth when moved (GRN's is continuous).
- AIR (grain_air 137): GrainCloud's `air`: per-grain detune +-18 cents x air (a chorus), pan range + 0.7 x air; from
  0.25 up to 30% reverse grains (read from the end, step negative); from 0.4 up to 30% octave-up grains (not when the
  pitch is above +12); the cloud high-passed at 30 + 220 x air Hz and a +4 dB x air shelf over ~3.5 kHz (before the
  level match, so the level holds); above 0.5 the cloud's output is added into this block's ring (fb 0 .. 0.6, tanh).
  Only plain grains are WSOLA-aligned and become `last_`. Engine test: level -0.7 / 0.0 dB at 50 / 100%, no runaway.
- PITCH: real_of no longer rounds; apply_param rounds when pitch_snap (138) is on; the display shows tenths, or whole
  semitones with SNAP. Both engines glide (one-pole per block, ~50 ms); GrainCloud updates active grains' step (and
  their anti-alias low-pass) from each grain's `mult` (its detune/octave), so held grains bend with the knob.
- Tiles: sw_1..3 (139-141), a 1x3 `list` at x 968 beside the tabs, art sw_88(_on) (amber: a live switch). Like the
  tabs: lit by the plugin (reported every time), any write that isn't MPC's echo flips SCAN / scan_mode / pitch_snap.
  The SCAN and GRAINS toggles are gone from the row: seven knob slots ~100 apart (598 .. 1202).
- The strip: the kit's pictures can't take `when=` and are opaque, so the playhead picture (glow5, 668 x 30) is gone
  and 12 pictures spark_0..11 (142-153, 52 x 30 at the columns' x) draw both: state = streaks 0..3 + 4 x (orb none /
  left half / right half). 12 states (the Force has shown pictures of 25). ReverbEngine::spark(c): GrainCloud::activity
  sums each grain's window into the column it reads (as Position maps: 0.03 s + x x depth back), levels > 0.05 / 0.6 /
  1.5. notify() sends changed columns at most every 6 blocks; P_PLAYHEAD is still computed and reported (tests).
- Preset-pick test: Automate cap 32 -> 40 (the new settings and tiles add 5 reports).
- 5b (the user's photo of test 5 on the device): the SCAN tiles covered the sonics line (cx 1060, w 400, beside the
  tabs): it now follows pg_info (154: the keyboard closed and not the SCAN tab). The streaks showed while silent (grains
  run over silence): each grain keeps `lev` (its windowed power, ~5 ms) and activity weighs it from -70 dB (nothing) to
  -40 dB (full). Open: on the SCAN tab only the first column of the painting and of the strip showed (columns 2-12
  missing); asked the user for the SHAPE tab and whether dragging moves the visible part.
- 5c: the user's 5b photo: still only the first column on the SCAN tab (the sonics line fixed). The one widget over the
  painting there is SCAN's drag strip (slider_h 668 x 148): the kit's film strip for it was 83 frames, 668 x 12284 px
  (~32 MB as RGBA, 40% of the skin's ~83 MB). skin_post now replaces it with sh_scan_drag_clear.png (2 clear frames,
  numFrames 2). If the painting still breaks there, the next step is to drop the drag strip.
- 5d, the real cause (5c's smaller drag strip didn't fix it; kept, it saves 32 MB): the kit bakes each `when=`
  condition's art (captions, a list's tile backs) into ONE opaque picture cut from the background, drawn in the order
  the condition first appears in layout.conf. The new sw tiles (y 100) stretched pg_scan's picture from the caption
  (y 322) up to the tiles: x 630-1236, the painting's whole height, drawn after the painting: columns 2-12 covered.
  Fix: the sw list and the pages' captions come before the painting's pictures in layout.conf. skin_post now fails
  the build if any picture other than the keyboard's is drawn after the painting's or strip's and overlaps them (it
  caught pg_shape's caption art covering the strip's bottom 8 px too). The page renders all 12 columns again.
- 5e (the user: "it works now"; asked for smoother, more fluid streaks, only if still true to the grains): the jumps
  came from coarse steps, not the rate (~58 Hz already): each grain now shares its weight between the two nearest
  columns by where it reads (it glides across), each column's activity is smoothed (up ~15 ms, down ~120 ms, about a
  grain's own fade; cleared at once when GRAINS stops), 8 levels on a soft curve (kSparkLevels), and the art (strip
  v7) nests: each level is the one below plus streaks, brighter. 24 states (+8 x orb half). Engine test: POSITION
  swept across in 4 s, the brightest column follows (1 -> 11) with no column changing over 2 levels between reports.
- 5f (the user on 5e: smoother, but "too many stripes ... I hoped 1 stripe per movement", each gliding on its own, as
  granulators draw grains): the strip is no longer a density meter. GrainCloud::stripes: per column, the loudest grain
  heard (lev over -60 dB) reading in it, at its place across the column (1..7, ~7 px). Each stripe is one real grain
  where it reads: still at PITCH 0 (it reads at the speed the sound arrives), drifting when pitched. Up to 12 at once
  (one per column: the pictures can't overlap, and 24 states each is the Force's limit with the orb's halves).
  Strip v8 art. The 5e smoothing (activity, spark_act_) is gone; activity() stays (unused by the display).

### 1.8.0 test build 6 (2026-11-25): GRAINS' SYNC
- The user asked for more GRN-like features; I advised the plugin is near saturation (AIR already bundles GRN's
  jitter / spread / reverse / filter) and proposed only SYNC, then stopping features to finish 1.5.0. The user: yes.
  (On 5f's single stripes vs 5e's bunched ones the user is undecided: they'll live with 5f a session.)
- sw_4 (142, inserted after sw_3: the sparks and pg_info moved up one; all unreleased) and grain_sync (157). The
  tiles row is 4 x 88 at x 880. DENSITY with SYNC: kGrainBeats / kGrainNotes (8 steps of the knob), its display
  the note; apply_param sets ReverbParams::grain_beats (0 = free).
- Transport: process_replacing asks audioMasterGetTime for kVstTempoValid | kVstPpqPosValid when any sync is on and
  calls ReverbEngine::set_transport(ppqPos, tempo, playing) before process(); block() advances tr_ppq_ per 128.
  GrainCloud: playing, a grain whenever floor(ppq / beats) changes (exact sample within the block; MIDI_TIMING.md's
  advice: from song position, never counted); a step back within half a step of the last grain is a loop's wrap
  (the block before ran past the loop end and fired it) and is skipped; stopped, every beats / pps samples.
- Tests: engine (120 bpm: 1/8 from ppq 3.3 -> 19 grains in 4.7 s, stopped 18, 1/16 T 56, through a 3-beat loop 18:
  no doubles at the wrap); plugin (the SYNC tile, DENSITY as "1/8" / "1/32", back to % when off, saved, reset).
- The pre-delay SYNC test once read +27 ms (a paced-test timing blip): 3 reruns with this code exact.

## 1.5.0 release (2026-11-25): everything since 1.4.1 as one release
- The user tested test build 6 on both units ("it all seems to be good and working") and asked to package everything
  for the repo and the catalog. The work above (keyboard 1.5, FREEZE / END SYNC 1.6, ROOM 1.7, SCAN 1.8, the test
  builds) ships as **1.5.0**, one squashed commit on main (afeb591), one changelog entry, the 1.5 manual.
- Added for the release, at the user's request ("on each version install my presets would be deleted"): your files
  live outside the plugin's folder. Library::data_dir() = "<drive>/Liminal Hz" (the first folder under media_root()
  that holds the plugin folder; made if missing; MPCNAM_DATA overrides; else the plugin folder, as before). My Presets
  (Presets::dir) and TONE3000 downloads (Tone3000 dest_) go there; own_dirs() scans its reverbs/ first.
  Library::migrate_user_files() runs once per start before the first scan (std::call_once in the worker): entries of
  <plugin>/presets and <plugin>/reverbs move there by rename() unless that name exists (then they stay, still found);
  README.txt stays. The packaged presets/ and reverbs/ READMEs say where the files are now. library_test covers it.
- Parameter compatibility: the 64 parameters of 1.4.1 are unchanged in place; 93 appended (157). param_compat stays 1.
- Doc images re-rendered from the plugin (pageshot2: the factory IRs via MPCNAM_FACTORY=packaging/factory, a chord
  playing for the grains); banner / page / shape / controls remade by make_banners.py.
- Known flaky check (pre-existing, also seen in 1.4): plugin_test's pre-delay SYNC sample count reads +1207 samples
  about once in five runs; reruns pass.


## 1.5.0: Akai SDK checks again (2026-11-25)
- Checked against inmusic-software/mpc-plugin-builder-sdk e78bad5 (validate.py, host_sim) and open-mpc/mpc-vst-plugins
  c148e63 (its release workflow runs validate.py as advisory only; host_sim is the SDK's own "must pass").
- validate.py had 1 warning: no MPC Sample Knob FX mapping. package_official.py now writes `shortName`, `knobs`
  (Preset 39, Length 5, Mix 0) and `shiftKnobs` (Pre-delay 1, Low Cut 2, High Cut 3) into plugin.json. As permanent as
  the parameter order. Not tried on an MPC Sample.
- host_sim (1.4.1 passed it) failed on 1.5.0:
  - sweep: the SW tiles flipped on every write, so the simulator's repeat write flipped them back. A write within
    kSwRepeatMs (60 ms) of a tile's last flip is now the same tap (plugin_test: two writes at once = one flip);
  - state round-trip: set_chunk closed the lists but left P_PRESET_OPEN's flag (it read "Open"); now cleared too;
  - silence pass: ~4600 output samples below 1e-30 (FTZ was on: tiny normals, not denormals). ReverbEngine::process
    now writes |x| < 1e-20 as 0.
- Now: `host_sim test` (seed 1234, its default) PASS at blocks 128, 32, 96. Other seeds / block 512 can still list
  params that a project deliberately doesn't restore: Menu, the list rows, Preset List, Tab and the pages, Freeze,
  Scan (the CHANGELOG's "a project opens on the page, not frozen, SCAN off"), and the meters (Playhead, Sparks), which
  follow the audio. 1.4.1 shows the same with other seeds (Menu, Preset List, List Row).
- The upstream catalog checker (open-mpc tools/catalog_check.py) warned that the per-CPU zips had no plugin.json, so a
  catalog install into `VST2 Plugins` would not be listed on MPC OS 3.10.1. CI's kit pin moves from 7c4bf25 to
  20df019 (open-mpc main, 2026-11-13; the same tree as gmorb/mpc-vst-plugins f1b2f90), whose release.py writes it. The
  device .so files built with it are byte-identical to the ones every test ran on (host, ASan, TSan, armv7, aarch64).
  That kit also writes `vst/params.lock` (the parameter keys in order; a build fails if the order changes): committed.
  Its skins' version.xml says 0.1.0.0 (was 1.0.0.0). package_official.py ignores the per-CPU plugin.json (it writes
  the 3.10.1 folder's own). Left: install.sh / uninstall.sh "differ from the current template" (newer upstream than the
  pin), network flagged (the entry's flags_reviewed has it), GLIBC 2.34 = MPC OS 3.x only (as 1.4.1).

## 1.5.0: Bend, Shimmer, Motion, Age on ROOM (2026-11-26; asked for by the user, to ship in 1.5.0)
- GRAINS on brings SCAN on (grains_switched); GRAINS off takes it off again unless SCAN was tapped since.
- engine/shifter.h: PitchShifter, the two-tap delay-line shifter (Hann taps half a window apart, linear reads).
  BEND (-12..+12 st, 70 ms window) on the wet after SCAN: ratio glides (kBendGlide 60 ms), and at 0 the path fades out
  over 30 ms and the wet is bit-identical (engine_test). SHIMMER (0..1): the wet +12 (100 ms window), HP 350 Hz, LP
  9 kHz, soft-clipped, fed into the next block's input with the feedback, under the same guard (fb_lim_) and scaled by
  fb_gain^kFbEase; with Feedback 100% too it stays bounded and dies away (engine_test).
- engine/color.h: ColorStage after BEND. MOTION: 3 chorus voices per side (8/13/19 ms, swayed 0.23/0.37/0.51 Hz, R a
  quarter turn on), equal-power mix up to 0.6. AGE: wow 0.6 Hz + flutter 7 Hz on a 3 ms delay, Pade soft clip, LP
  18 kHz -> 4.5 kHz. Phasors (no sin/tanh per sample, the SDK's CPU rule). Both bypassed exactly at 0.
- Params appended: bend, shimmer, motion, age (161). Q-Link 15: Bend (was Reverse; the user chose it), so the claim
  "Q-Links from 1.4 unchanged" is gone from the docs. ROOM row: room (Distance, Damping), pitch (Bend, Shimmer), color
  (Motion, Age); cap2_pitch / cap2_color captions.
- host_sim: with 4 more params the default seed's random state now lands on Freeze / Tab / Preset List in the state
  round-trip (the by-design list above); the sweep, denormals and every other check pass.
