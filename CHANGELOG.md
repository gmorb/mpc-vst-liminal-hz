# Changelog

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
