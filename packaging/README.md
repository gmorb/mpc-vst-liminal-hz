# Liminal Hz for MPC OS

*Real rooms, halls and echoes from impulse responses: the space for your NAM amp.*

A convolution reverb for Gen 1 Akai Force and MPC units: load impulse responses of real rooms, halls, catacombs,
plates and outboard echoes (up to 5 seconds, mono or stereo) and play through them, with a touchscreen page to browse
the IRs on your cards, make a room bigger or smaller (Decay), keep your sounds (My Presets), and download more IRs
from TONE3000 from your phone. Version @VERSION@.
Not affiliated with or endorsed by TONE3000 or Akai Professional.

## What's in the folder
`Gm0rb - VST - Liminal Hz/` is the whole plugin: `liminal_hz.so`, its page (`Plugin Skins/`), `plugin-meta.xml`,
`version.xml`, `LiminalHz.json` (parameter list), licences, the factory IRs (`factory/`), the phone pages' font
(`art/fonts/`), an empty `reverbs/` folder for your own IRs and an empty `presets/` folder for My Presets.

## Requirements
- A Gen 1 Akai Force or MPC (Live, Live II, One, X, Key 61), with SSH access (modded firmware, e.g. MockbaMod)
- The `Synths` folder with `vstscanner.sh` from the Force VST plugins distribution
- Reverb IRs (.wav), e.g. from TONE3000 (Spaces) or your own

## Install
**Assumption: you use MockbaMod and its memory card is `/media/662522`** (otherwise see NAM A2's README,
"Other firmware": the steps are the same).

1. Copy the `Gm0rb - VST - Liminal Hz` folder into the `Synths` folder on the card (`/media/662522/Synths`).
2. Put IRs in a `reverbs` folder (see Your IRs).
3. On the device: `ssh ip-of-Force 'sh /media/662522/Synths/vstscanner.sh'` (later just `vstscanner`).
4. MPC restarts. The plugin is under VST, manufacturer "Gm0rb", as "Liminal Hz".

To update: replace the folder, keeping your `reverbs` and `presets` folders if they're inside it, then re-insert the
plugin on your tracks.

## Your IRs
The plugin looks for folders named `reverbs` (any capitalisation) inside its own folder and anywhere on your cards
and the internal drive, up to four folders deep. Subfolders are packs. After adding files, press RESCAN.
- Mono or stereo WAV; 16/24/32-bit PCM, 32/64-bit float, also "extensible" WAVs; any sample rate.
- Up to 5 seconds are used; longer IRs are cut to 5 s (faded out). LENGTH shortens them further.
- Stereo IRs (most reverb IRs) take the mono sum of the input and give their own left and right; mono IRs process
  left and right separately, keeping your stereo image.

## Presets and factory IRs
- **PRESET** (top of the IR block): tap it and a list opens over the page: **Init** (no IR, every setting at its
  default: the plugin passes your sound through) and 14 factory presets in three columns: **Spaces** (Empty Mall,
  Pool Rooms, Fluorescent Hall, Backrooms), **Echoes** (Stairwell Flutter, Tape Corridor, Dream Ping-Pong, Parking
  Slap, Intercom Echo) and **Strange** (Glass Void, Reverse Bloom, Sub Tunnel, Shimmer Fog, Liminal Hz). A preset
  loads its factory IR with settings to match; change anything afterwards.
- **MY PRESETS** (beside PRESET): your own presets (see My Presets below).
- **FACTORY / MY IRS** chooses what ◀ ▶, PREV/NEXT PACK and **IR LIST** step through: the factory IRs, or your own (`reverbs`
  folders and TONE3000 downloads). Switching never changes what's playing.
- The factory IRs are synthetic (made by a script, not recorded), free to use like the rest of the plugin, and can't
  be deleted from the page. They're in the plugin's `factory/` folder if you want them elsewhere too.

## The page
One page. The left column is the browser; the right is the **shape** of the sound; the controls sit below, grouped by
what they do. In the background, a doorway at the far end of an empty room.
- **PRESET** (top left) and **FACTORY / MY IRS**: see Presets and factory IRs above. **BROWSE** (in the TONE3000
  strip) opens the phone page (below).
- **IR LIST** (beside FACTORY / MY IRS): every IR of the list you are in, six to a page as "pack · name", over the
  browser. Tap a row to load it (the one in use is outlined); PREV / NEXT turn the pages; CLOSE puts the browser
  back. The list stays open when you pick. Long names slide along until their end shows.
- **The IR**: its name, its pack (a TONE3000 tone's title or your folder) and a detail line ("IR 2 of 5 · Local ·
  Stereo"; for TONE3000 IRs the gear type and creator). The T3K mark shows on a TONE3000 IR. A new IR crossfades in.
- **RESCAN** looks for new files. **DELETE**: tap twice to delete the IR in use **permanently** (the first tap only
  asks; it's forgotten after 4 seconds). Factory IRs can't be deleted.
- **shape**: what the IR sounds like, after your shaping. A line in words ("decay 2.8 s · airy · darkens · wide ·
  diffuse": how long it rings, its colour, whether it darkens or brightens, its width, echoes or a diffuse wash),
  then the IR itself as a painting: a misty trace whose height is its level and whose colour is its tone (pale ice
  with sparkles = airy, bone = warm, dusky violet = dark), so you can see a room darken as it fades. After each note
  a glow drifts through it (pre-delay included), showing where the sound is in the space: it hears hits, plucks and
  slow pads, and follows MIDI note-ons when the MPC sends MIDI to the plugin.
  - **trim**: Start and End. **fade**: Fade in, Fade out. **time**: Stretch (50-200%: longer and lower, or shorter
    and higher), **Decay** (50-300%: how long the room rings, band by band, without changing its pitch or colour;
    100% = as recorded; still within 5 s) and Reverse (below Decay: the IR backwards, it swells into the sound).
    Prepared in the background, crossfaded in.
- **space**: **Mix** (the big one), Pre-delay (0-250 ms), Feedback (0-100%: the reverb fed back into itself for
  longer, building tails; from about 70% a long ring held at about the level you play, fading out over 30-40 s once
  you stop; it never runs away, and each repeat is a little darker, like a tape echo). **tone**: Low cut, High cut
  (on the reverb only; each octave gets the same turn of the knob), Width (0-200%). **out**: Output. Mix, Width,
  Output and Pre-delay glide to new values (no clicks).

## My Presets
- Tap **MY PRESETS**: your presets, four to a page, over the browser (PREV / NEXT / CLOSE). **SAVE** keeps the
  current sound (every knob and the IR in use) under the IR's name; tap a row to load one; **DELETE** removes the
  selected one (tap twice). The PRESET field shows "My: <name>" while one is in use, and a project remembers it.
- **NAME ON PHONE**: the MPC has no keyboard, so names are typed on a phone on the same Wi-Fi: the line shows an
  address such as `192.168.1.20:8191/presets`. Open it to save the current sound under a name, or load, rename and
  delete presets. The same page is a button on the TONE3000 page too. It needs no internet.
- Presets are small text files in the plugin's `presets/` folder (`<name>.lhzp`), kept across upgrades; rename or
  delete them from a computer too, then press RESCAN.

## Getting IRs from TONE3000
1. Press **BROWSE**. The TONE3000 line shows an address, e.g. `Open 192.168.1.20:8191`.
2. On a phone on the same Wi-Fi as the MPC, open `http://` and that address, e.g. `http://192.168.1.20:8191`.
3. Continue, sign in to TONE3000 and pick an IR from the Space, Outboard, Pedal or Experimental categories (IRs
   only: only `.wav` IRs are downloaded). It downloads to `reverbs/TONE3000/<tone>/` and is selected.
The page also has a **My Presets: name & save** button (see My Presets).

## Notes
- Each plugin has its own page port (this one 8191; NAM A2 uses 8091), and a second instance takes the next free port
  (8191+1, ...), so several can be open at once: the TONE3000 line always shows the address to open.
- **CPU** (a Force, 5 s stereo IR): about 3.5% of an audio block on MPC's audio thread (at most ~7%), plus about 8%
  of another core for the IR's tail. Shorter IRs and LENGTH cost less on that core.
- If the second core is ever too busy, a tail block arrives late: the detail line then says "Tail late ...".
- Latency: none (the first part of the IR runs on the audio thread, in MPC's own blocks).

## Licence and source
MIT (`LICENSE`); `NOTICE.md` lists the components (PFFFT, AudioDSPTools' resampler, nlohmann/json, the Titillium
Web font) and their licences, with the texts in `licenses/` (the font's in `art/fonts/OFL.txt`). Source code: @SOURCE@
