# Changelog

## 1.4.0

Tested on an MPC Live III (Gen 2) and an Akai Force (Gen 1), both on MPC OS 3.10.

### New
- **Duck** (the out group, beside Output; 0-100%, default 0): the reverb is turned down while you play and blooms back
  in the gaps, so big rooms on snares and vocals don't muddy the groove. At 100% it drops by up to 24 dB under a loud
  input and is back within about a quarter of a second after it stops (one knob: the attack and release are fixed).
  It turns down only what you hear, after the feedback loop, so Feedback's tails are unchanged. At 0% the output is
  bit for bit as before. Kept by My Presets; Init and the factory presets set it to 0.
- **SYNC** (beside Pre-delay): Pre-delay in note values at the project tempo: 0, 1/128, 1/64, 1/32, 1/16 T, 1/16,
  1/16 D, 1/8 T, 1/8, 1/8 D, 1/4. The tempo is read from MPC every block, so it follows tempo changes (gliding, like a
  knob move). The pre-delay can now reach 2 s (a 1/4 at 30 BPM). Kept by My Presets; Init and the factory presets turn it
  off.
- **Textures**: a fourth factory category of IRs that aren't rooms: Struck Pipe, Grain Cloud, Tuned Chord, Breath,
  Bouncing Ball, each with a preset. The PRESET list now opens in four columns. Like the other factory IRs they are
  synthetic and free to use. (Any .wav can be an IR: README, Your own IRs.) Grain Cloud's grains are short bursts
  of band-passed noise, not sine tones, so it doesn't whistle on steady synth partials.
- **Preset levels**: every factory preset was measured over drums, a vocal, a synth chord, pink noise and a sine
  sweep. Each now peaks within ~3 dB of its input (none over 0 dBFS from a -12 dBFS sweep) and keeps its level within
  about +-3 dB of the dry. Resonant ones were tamed: Sub Tunnel (Mix 22%, Output -3 dB), Liminal Hz (Mix 30%,
  Feedback 45%, Output -2 dB), Struck Pipe (Mix 20%, Output -2 dB), Grain Cloud (Output -4 dB, Width 120%), Tuned
  Chord (Mix 12%, Output -2 dB, Width 100%), Breath (Output -5 dB, Width 110%); Glass Void's Width is 115% so it holds
  up in mono. Projects keep their own settings; only the presets changed.
- **MPC's own preset menu** (at the top of the screen) now lists Init, the 19 factory presets grouped by what they're
  for (Drum, Vocal, Keys, Pad, FX: "Drum: Empty Mall", "Keys: Liminal Hz"), then **your own IRs** ("My IR: Big Hall",
  each with Init's settings), read when the plugin loads, so IRs added or deleted show up on the next load. Picking a
  factory preset there is the same as picking it in the plugin's PRESET list. Re-selecting the current one changes
  nothing, and a pick in the first 2 s after a project's state arrives is ignored (a host re-selecting what it saved,
  whose place may have moved): a project always reopens with its own sound.
- **Picking one of your own IRs goes back to Init**: on the page (arrows, packs, IR List, a TONE3000 download) or
  from MPC's menu, after a factory preset or after another of your IRs, the knobs return to Init's: a sound's settings
  belong to its IR. Picking the same IR again keeps your edits, a project reopens as saved, and My Presets load as
  saved (their own IR and settings).
- **What each IR suits**: under the IR's name, a factory IR's category now says what it's best on ("Strange · best
  on chords, pads"); the README has the table.
- Seven more demos (the five Textures, Duck, SYNC), a trailer kit (`tools/trailer/`: three synthesized scores,
  dark, cyberpunk and trap, played through the plugin, and the videos), and images explaining the new features (`tools/make_feature_images.py`).
- **Sound-design demos**: six more in the README, for ambience, FX and ear candy (a whisper, water drops, wind, clicks, a
  riser and an old music box through Reverse Bloom, Struck Pipe, Tuned Chord, Bouncing Ball, Grain Cloud and Breath,
  pushed with Stretch, Decay, Feedback and SYNC). The banner (also the repo's social preview) says what's new, and a
  "New in 1.4" card sums it up.

### Changed
- **The pause when picking a preset or after DELETE.** MPC answers each "update the display" call from a plugin by
  reading every parameter again and redrawing its screen. A preset pick asked for that twice (once for the pick, again
  when the IR was ready a moment later) and reported 31 values; turning a knob asked for it on every step. Now these
  calls are paced (at most one per 0.1 s, and one waits up to 0.6 s for the IR to be ready), and values MPC already has
  aren't reported again: a pick is one redraw and 18 values. DELETE took the IR out by searching the cards again
  ("Searching..."); it now drops it from the list at once and moves to the next IR. Needs confirming on the device.
- The resampling cache keeps the last 4 IRs (it kept 1), so stepping back and forth through 48 kHz IRs resamples each
  once.
- **Q-Links** reordered, what's turned while playing first: Mix, Decay, Feedback, Pre-delay, Duck, Low Cut, High Cut,
  Width, IR, Stretch, Start, End, Fade In, Fade Out, Reverse, Output (was: IR, Mix, Pre-delay, Feedback, Low Cut, High
  Cut, Width, Output, Start, End, Fade In, Fade Out, Stretch, Reverse, Next Pack, Decay). Next Pack is gone from the
  Q-Links (PREV/NEXT PACK are on the page).
- The bottom row is respaced to fit DUCK and SYNC.
- **LONG IR is an on/off switch like REVERSE** (it was two taps, the first a warning): on the device each tap reached
  the plugin twice, so it came on at the first tap and warned again on the way off. It is now the same switch as
  REVERSE, with "Long IR" written under it, and the line below reads "On · CPU heavy · 20 s" while it's on. (A first
  fix used a plain button, which MPC treats as a momentary trigger: it flipped the setting twice per tap and never
  showed it on.)
- **DELETE** (an IR, a My Preset) still takes two taps, and now the second must come at least 0.3 s after the first,
  so one touch delivered twice only asks.
- SYNC's label is the parameter's name, which MPC writes under the switch: "Pre-delay Sync" didn't fit, it's "Sync".
- Tests: plugin_test times a preset pick's setParameter and counts its host calls, checks DELETE never searches, SYNC
  at two tempos and without one, Duck and SYNC in projects, presets and Init, and the Textures presets; engine_test
  checks Duck (bit-identical at 0, -24/-12 dB, the release, Feedback tails untouched) and the 4-entry resample cache.

### Fixed
- **One tap is one step** (issue #5): NEXT PACK and PREV PACK, the IR List's and My Presets' PREV and NEXT, CLOSE,
  DELETE, SAVE, RESCAN and BROWSE took a step when pressed and another when released, so paging skipped
  every other pack or page. MPC's button switched the value twice per tap (once on touch, once by the button itself);
  it now switches it once, as REVERSE and LONG IR do. Thanks to ekeppel for the report (MPC Key 61, MPC OS 3.10).
- **Stretch 200% with Decay** (Gen 1, LONG IR off) made Grain Cloud, Tuned Chord and Shimmer Fog a "Silent IR" at any
  Decay over 100%: stretching leaves a band-limited IR's top octaves all but empty, an empty band's measured decay time
  is noise, and lengthening it overflowed a float. Such a band is now left as it is. Every setting that worked before
  prepares bit for bit as before (all 19 IRs at Stretch 50/100/200%, Decay 50/300%, LONG IR off and on); engine_test
  prepares the factory Textures at every Stretch x Decay.

## 1.3.0

### New
- **LONG IR** (Gen 1): a button under Reverse that brings Gen 2's IR quality to the Force and Gen 1 MPCs: IRs up to
  **20 s** instead of 5 (so Decay 300% on a 5 s room rings its full ~15 s), band-limited sinc resampling for IRs at other
  sample rates and for Stretch (no dulled top octave, no alias tones), and Decay analysed at twice the frequency detail.
  It is CPU heavy, so it takes two taps: the first only warns ("CPU heavy · tap again"), a second within 4 s turns it on;
  one tap turns it off. It's a device setting: a project remembers it, presets and Init leave it alone, and it is never
  automation. On Gen 2 it reads "Gen 2 · always on".

### Changed
- **Gen 1 is cheaper to run, with LONG IR off too:**
  - the third ("far") convolution stage, until now Gen 2 only, is on in both builds. Past 0.74 s an IR runs in
    16384-sample blocks on its own worker, so a 5 s IR needs about a quarter of the partitions it did;
  - the FFT runs PFFFT's NEON code. Until now the Gen 1 build ran PFFFT's scalar code, because the build flag that
    enables NEON was never set.
  Both give the same result as before (equal to direct convolution, `tools/bench_reverb --check`); only the CPU cost drops.
  On a PC (`tools/bench_reverb`, real time, 5 s stereo IR) the worker's load falls from 1.9% to 0.7% of a core, and a
  20 s IR with LONG IR on costs 1.3%: less than a 5 s IR did in 1.2.0. The audio thread's cost is unchanged. Not yet
  measured on a Force (docs/HANDOFF.md lists what to measure).
- **Faster re-preparation:** the resampled IR is kept, so turning Start, End, the fades, Decay or Reverse no longer
  resamples the file again (only a new file, Stretch or LONG IR do). With LONG IR on, a 20 s 48 kHz IR re-prepares after
  End, a fade or Reverse in about 0.12 s instead of about 1 s on a PC.
- `engine/platform.h`: the IR quality (`Quality`: length cap, sinc, Decay FFT) is now a runtime setting instead of
  build constants; `LIMINAL_GEN2` makes the high quality the only one.
- Tests: the high-quality tests run in both builds; `ARM=1` now also runs the convolution check and the engine tests
  as armv7 programs (NEON) under qemu-arm; plugin tests for LONG IR's two taps, its time-out, projects, Init and presets.

## 1.2.0

### New
- **Gen 2 build (MPC Live III and other RK3588 units, 64-bit).** The release now carries a second zip,
  `Liminal-Hz-<version>-mpc-aarch64.zip`, next to the Gen 1 one: the same plugin (same id, uid, parameters, presets
  and page), so a project moves between generations. Its installer registers it under `pluginList-arm-64bit`.
- **Gen 2 lifts the Gen 1 limits** (engine/platform.h), since the RK3588 has far more headroom than the Force's RK3288:
  - **IRs up to 20 s** (Gen 1: 5 s). Long halls, drones and long echo IRs play whole.
  - **Decay reaches its full length**: 300% on a 5 s room now rings to about 15 s; on Gen 1 it stopped at the 5 s cap.
  - **A third convolution stage** for long IRs: taps past 0.74 s run in 16384-sample blocks on their own worker
    thread, so a 20 s stereo IR costs the audio thread exactly what a 5 s one does (the same 93 ms zero-latency head),
    and the background work is a fraction of what 2048-sample blocks would need. Still zero latency, still equal to
    direct convolution (`tools/bench_reverb --check`: about 133 dB below the signal).
  - **Band-limited IR resampling** (Kaiser-windowed sinc, 64 zero crossings) instead of cubic interpolation, for IRs
    at other sample rates and for **Stretch**. A 48 kHz IR keeps its top octave (cubic lost 1.9 dB at 17 kHz) and
    Stretch below 100% no longer folds high partials back down as alias tones (cubic: a full-level alias; now -77 dB).
  - **Finer Decay analysis**: an STFT of 4096 (10.8 Hz bins) instead of 2048, so the bass third-octave bands are
    measured and continued from real resolution; same 11.6 ms time step.
  - **NEON FFT** (PFFFT's NEON path; the Gen 1 build has always used PFFFT's scalar code).
- Tested on an MPC Live III (Gen 2) and an Akai Force (Gen 1), both on MPC OS 3.9.1.
- Tests: `GEN2=1 tools/test.sh` (the Gen 2 limits on the PC), `AARCH64=1` (the convolution check, engine tests and the
  plugin test as aarch64 programs under qemu-aarch64, against the Gen 2 .so); new tests for the far stage, 15-20 s
  IRs, Decay past 5 s, and the resampler's top octave and aliasing.

### Changed
- `engine/long_convolver`: the tail is now a list of stages (each with its own worker). The Gen 1 build has one stage,
  as before; its tests pass unchanged.
- The aarch64 build sets FPCR's flush-to-zero while processing, as the armv7 build sets FPSCR's.

## 1.1.0

### New
- **One page, two lists over the browser.** **IR LIST** (beside FACTORY / MY IRS) shows every IR of the list you are
  in, six to a page as "pack · name": tap one to load it, PREV / NEXT / CLOSE. **MY PRESETS** (beside PRESET) shows
  your presets, four to a page. The list stays open while you try one after another; long names slide along until
  their end shows. BROWSE is now a small button in the TONE3000 strip.
- **My Presets.** SAVE keeps the current sound (every knob and the IR in use) under the IR's name; tap a row to load
  it; DELETE (two taps) removes it. The PRESET field shows "My: <name>" while one is in use, and a project remembers
  it. Presets are `presets/<name>.lhzp` text files, kept across upgrades and renamable from a computer (RESCAN).
- **Names typed on a phone.** NAME ON PHONE opens a page on the plugin's phone server (`<address>/presets`, no
  internet needed): save the current sound under a typed name, load, rename, delete. The TONE3000 home page has a
  **My Presets: name & save** button too.
- **Decay** (50-300%, in the time group beside Stretch; Reverse moved below it): how long the room rings, scaled
  band by band (third octaves) when the IR is prepared, with each band's tail continued in kind where the recording
  ends. No pitch change, the room keeps its colour; 100% plays the IR exactly as recorded. A 1.0 s test room measures
  0.50 / 1.00 / 1.94 / 2.93 s at 50 / 100 / 200 / 300%. Within the 5 s cap. Factory presets set it to 100%; older My
  Presets and projects load at 100%.
- **Phone pages in the Liminal Hz look**: near-black with a warm glow, thin frames, the LIMINAL Hz wordmark and
  Titillium Web (shipped in the plugin folder's `art/fonts`, with its OFL licence).
- **TONE3000**: the picker offers the Space, Outboard, Pedal and Experimental categories, IRs only; only `.wav` IRs
  are downloaded and a tone that isn't an IR is refused. The welcome page is reworded ("Liminal Hz can load impulse
  responses (IRs) from TONE3000 ...").
- **The playhead catches more notes**: it listens for new notes in the spectrum, so soft attacks and pads played over a
  long release restart it too (in tests: pads 7-8 of 8, plucks 16 of 16; before, no pad note and 6 of 16 plucks),
  while a held, beating chord or tremolo doesn't. It also follows MIDI note-ons when the MPC sends MIDI to the plugin.

### Changed
- **Feedback** (now 0-100%) can't run away any more. It used to go over unity at the IR's loudest frequency from about
  a quarter of the knob and grow into a loud, endless drone (up to tens of dB over the dry signal). The loop is now
  scaled to each IR's spectrum, eases in over the knob (about 2-4x longer tails around 50%, a long ring from about
  70%), is held at roughly the level you play at and fades out over 30-40 s once you stop, and each repeat is a little
  darker (a 4 kHz damping filter, like a tape echo). The factory presets that use Feedback were retuned to keep their
  tails; Liminal Hz's endless drone is now a long tail that ends. Saved sounds that use Feedback will sound different.
- **Low Cut and High Cut turn on a log scale** (each octave the same turn: 141 Hz and 4.5 kHz at the middle). Projects
  and presets keep their frequencies; Q-Link automation recorded on these two knobs plays back differently.
- **No clicks when turning Mix, Width, Output or Pre-delay** (they glide to the new value).
- **Faster start and RESCAN**: the search for `reverbs` folders reads each folder once, skips system folders, and
  remembers where it found them (`.reverbs-index` next to the plugin), so the saved IR loads at once while the full
  search runs behind it. About 3.5x faster on a full card in tests, more on a slow SD card. Thanks to Locrian, whose
  suggestion (the MpcSettings install-folder helper) started this; the helper itself wasn't needed.
- Larger text in the PRESET menu.

### Fixed
- Feedback's runaway (above).
- Tests cover all of this (My Presets files and phone page, the lists, Decay on IRs of 0.01-9 s, Feedback's bounds,
  the playhead's note detection and MIDI), with AddressSanitizer and ThreadSanitizer.

## 1.0.0
- Repo: a README with images (the Threshold banner, the page, the shape display, the controls) made by
  tools/make_banners.py from the plugin's own art and real data.
- Larger again: IR name 52, preset 40, knob values 38, knob names 27, pack 34, details 30, sonics 32, TONE3000 27;
  the LIMINAL Hz wordmark larger, with a more readable "by Gm0rb".
- Larger text for the Force's screen (it draws text smaller than it looks in mockups): IR name 48, preset 36, knob
  values 32, knob names 24, pack 30, details 26, sonics 28, TONE3000 status 24; taller text boxes (label_scale 1.4).
- Fix: the shape painting and its glow didn't show on the Force: the glow picture lay over the painting, and MPC's
  pictures are opaque. The glow now drifts along its own strip under the painting; the columns have small gaps.
- Larger again (IR name 44, preset 30, values 26, labels 18; knobs Mix r58, Pre-delay/Feedback r54, tone/out r50,
  shape r40). The shape painting: 12 columns edge to edge and a 24-step glow (the 16-column overlapping version with a
  33-state glow didn't show on the Force; this keeps to what's proven there), and no sparkles: air is a paler,
  translucent mist that thins towards the edges.
- Bigger: larger type throughout (IR name 36, preset 26, values 22, labels 15) and knobs (Mix r58, Pre-delay and
  Feedback r52, tone and output r44, shape r32), for reading at arm's length.
- The shape display is a painting of the IR: a misty, jagged trace (audio, zoomed in) per column, coloured by tone;
  columns overlap so it reads as one surface. A glow (32 positions, smoother than before) drifts through it after
  each note, instead of a bar under it.
- The page, reworked: an asymmetric layout (the browser on the left, the shape of the sound on the right, controls
  grouped as space / tone / out with Mix as the big knob), and the "Threshold" background: a doorway glowing at the
  far end of an empty room, its light on the floor, with drifting air and faint wavefronts (tools/make_backgrounds.py).
- The shape display now shows the sound: 16 columns, height = level (66 dB range: tails stay visible), colour = tone
  (airy, warm, dark), and a line in words (decay time, colour, darkening or brightening, width, echoes or diffuse).
  A decay longer than the IR itself reads "decay 4.6 s+".
- Page, reworked: an asymmetric layout with MIX as the large knob and the controls grouped by job (trim, fade,
  time, space, tone, out); the SHAPE display shows the sound: 16 columns of level, coloured by tone (airy, warm,
  dark) so a space darkening as it decays is visible, a time ruler, and a one-line summary ("decay 2.8 s · airy
  · darkens · wide · diffuse"). Background: a far doorway (lower right) with its light spilling into the room,
  receding frames, drifting air and faint wavefronts leaving the doorway.
- Design pass (guided by the design skills of Emil Kowalski, Jakub Krehel, Meng To and tastemaker): custom
  engraved knobs (128-frame filmstrips: matte body, tick scale, ivory pointer, amber value arc), amber reserved for
  live values (knob arcs, playhead), a neutral waveform, quiet hairline frames and spaced section labels, type roles
  for MPC's live text (names bright, details grey, labels dim), and a LIMINAL Hz wordmark.
- 14 factory IRs, synthetic and free to use (tools/make_factory_irs.py): Spaces (Empty Mall, Pool Rooms, Fluorescent
  Hall, Backrooms), Echoes (Stairwell Flutter, Tape Corridor, Dream Ping-Pong, Parking Slap, Intercom Echo), Strange
  (Glass Void, Reverse Bloom, Sub Tunnel, Shimmer Fog, Liminal Hz).
- PRESET menu (opens over the page, in 3 columns by category): Init (no IR, default settings: the dry signal) and
  a preset per factory IR with its own settings. SOURCE: Factory / My IRs (your reverbs/ folders and TONE3000).
  A fresh plugin starts as Init. Factory IRs can't be deleted.
- Factory IRs: 14 synthetic atmospheric spaces, echoes and strange rooms (tools/make_factory_irs.py: smooth
  frequency-dependent decay, early reflections, deliberate oddities), in factory/<Spaces|Echoes|Strange>/.
- PRESET list (a popup on the page): Init (no IR, defaults: the dry signal) and a preset per factory IR. A fresh
  plugin is Init. FACTORY / MY IRS: which IRs the browser steps through. Factory IRs can't be deleted.
- Fixed: clearing the IR (Init, or a file that fails to load) now fades the playing IR out; it used to keep playing.
- "Tail late" shows for 5 s after a late block, instead of for the rest of the session.
- Name: "Liminal Hz" by Gm0rb (in MPC and the catalog); earlier test builds were "IR Reverb" and "Echo Vault IR"
  (their projects still open).
- IR shaping (as in Kilohearts' Convolver): Start and End trims, Fade in, Fade out, Stretch (50-200%: longer and
  lower, or shorter and higher) and Reverse, all applied while the IR is prepared (no cost on the audio thread);
  Feedback (0-90%, soft-limited so it can't run away).
- SHAPE display: the shaped IR as a 24-column waveform (48 dB range), and a playhead that crosses it (pre-delay
  included) after each note; read-only, not automatable, screen updates at most every ~26 ms.
- A darker, textured page (generated by tools/make_art.py).
- The phone page uses this plugin's own port (8191) and, when that's taken (another instance), the next free one;
  a page that closed itself after 15 idle minutes opens again on the next press (it used to stay closed).
- Brand: the plugin is listed under the manufacturer "Gm0rb" (its folder: `Gm0rb - VST - Liminal Hz`).
- DELETE (in each block's header): deletes an IR permanently, in two taps (the first asks: "Tap DELETE again to
  delete it permanently", for 4 s). When the last file of a downloaded TONE3000 pack goes, its tone3000.json and
  folder go too. Only .nam / .wav files inside the scanned folders can be deleted.
- First release: convolution reverb for impulse responses up to 5 seconds (rooms, halls, spaces, outboard echoes),
  mono or stereo, for Gen 1 Akai Force / MPC.
- Long IRs cost little on the audio thread: the IR's first 93 ms are convolved there in 128-sample blocks (no
  latency), the tail in 2048-sample blocks on a second thread (another core). Measured on a Force with a 5 s stereo
  IR: 3.4% of an audio block on average (7.2% at most), the second thread 8.3% of a core, no late blocks.
- Mix, pre-delay, low and high cut, width, length, output. IRs are level-matched; switching IRs crossfades.
- Browses .wav IRs in any `reverbs` folder on the device's drives (and the plugin's own), with packs; reads stereo,
  64-bit float and the "extensible" WAVs many DAWs write. BROWSE TONE3000 downloads reverb and space IRs.
