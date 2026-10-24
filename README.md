![Liminal Hz by Gm0rb](docs/images/banner.png)

# Liminal Hz for MPC OS

*Real rooms, halls and echoes from impulse responses: the space for your NAM amp.*

A convolution reverb for **Akai MPC and Force, Gen 1 and Gen 2**: play through impulse responses of real rooms, halls,
strange spaces and echoes, mono or stereo: up to 5 seconds on Gen 1 (Force, MPC Live/One/X/Key), or **20 seconds**
with its **LONG IR** button, and always 20 seconds on Gen 2 (MPC Live III and other RK3588 units, 64-bit) (see
[Gen 2 and LONG IR](#gen-2-rk3588-64-bit)). Make a room bigger or smaller with **Decay**, stretch its tail
with **Feedback**, and keep your sounds as **My Presets**. It comes with 14 factory spaces, browses your own IRs, and
downloads more from TONE3000 from your phone. A companion to [Liminal Amp](https://github.com/gmorb/mpc-vst-liminal-amp).
Not affiliated with or endorsed by TONE3000 or Akai.

## What it looks like

![The Liminal Hz page](docs/images/page.png)

One page, made for the MPC's touchscreen, with nothing hidden on other tabs:
- **Left, the browser**: the **PRESET** menu with **MY PRESETS** beside it; **FACTORY / MY IRS** with **IR LIST**
  beside it; the IR in use with **◀ ▶** to step through the list, its pack and details; **PREV PACK / NEXT PACK** and
  **DELETE** (for your own IRs: it asks twice); and the TONE3000 strip with **BROWSE** and its status line.
- **Right, the shape** of the sound, and the shaping knobs: **trim**, **fade** and **time** (Stretch, Decay, Reverse).
- **Below**, the mix controls: **space**, **tone** and **out**; in the corner, **LONG IR** and its status line (20 s IRs
  on Gen 1: it asks twice, as it is CPU heavy).

Behind it all, a doorway at the far end of an empty room.

## IR List

![The IR List](docs/images/ir-list.png)

For long lists, so you don't have to tap ◀ ▶ over and over: tap **IR LIST** and every IR of the list you are in
(FACTORY or MY IRS, switched at the top) covers the browser, six to a page as "pack · name". Tap a row to load it (the
one in use is outlined); **PREV / NEXT** turn the pages; **CLOSE** puts the browser back. The list stays open when you
pick, so you can try one after another. Names too long for their row slide along slowly until their end shows.

## See the shape of the sound

![The shape display](docs/images/shape.png)

The shape display shows what the IR sounds like, after your shaping:
- **A line in words**: how long it rings, its colour, whether it darkens or brightens, how wide it is, and whether
  it's echoes or a diffuse wash ("decay 4.8 s+ · bright · darkens · stereo · echoes").
- **A painting of the IR**: its height is the level over time; its colour is its tone (pale for airy, bone for warm,
  dusky violet for dark), so you can watch a room darken as it fades.
- **A glow** that drifts along under it after each note you play, showing where the sound is in the space. It listens
  for new notes in what you play (hits and plucks, and also pads with slow attacks played over a long release), and
  follows MIDI note-ons too whenever the MPC sends MIDI to the plugin.

## The controls

![The controls](docs/images/controls.png)

- **space**: **Mix** (the big one), **Pre-delay** (0-250 ms) and **Feedback** (0-100%). Feedback feeds the reverb back
  into itself for longer, building tails: gentle at first, about 2-4x longer around 50%, and from about 70% a long ring
  that holds at roughly the level you play at, then fades away over 30-40 s once you stop. It can't run away or jump
  in level, and each repeat comes back a little darker, like a tape echo.
- **tone**: **Low cut** and **High cut** (on the reverb only; each octave gets the same turn of the knob) and
  **Width** (0-200%). **out**: **Output**.
- **trim**: **Start** and **End**. **fade**: **Fade in** and **Fade out**.
- **time**: **Stretch** (50-200%: longer and lower, or shorter and higher), **Decay** (50-300%) and **Reverse** (the IR
  backwards: it swells into the sound).
- **Decay** changes how long the room rings without changing its pitch or its colour: each frequency band of the IR
  is measured and made to fade faster or slower, and where a band's real tail ends it is continued in kind. A small
  dark room stays a dark room, just bigger (Backrooms at 300% rings about 2.5x as long). At 100% the IR plays exactly
  as recorded. IRs are capped at 5 s on Gen 1 (for the RK3288's CPU), so long IRs gain less there; with LONG IR on,
  and always on Gen 2, the cap is 20 s, so a 5 s room at 300% rings its full ~15 s. Feedback can take the tail further.
- **LONG IR** (lower right, under Reverse; Gen 1): 20 s IRs and the high-quality IR preparation of Gen 2. CPU heavy, so
  two taps: the first warns ("CPU heavy · tap again"), the second (within 4 s) turns it on; one tap turns it off. A
  project remembers it; presets and Init leave it alone. On Gen 2 it reads "Gen 2 · always on".

Shaping happens in the background and crossfades in. Mix, Width, Output and Pre-delay glide to a new value, so moving
them never clicks.

## Factory presets

Tap **PRESET** for a menu of 14 presets in three columns, plus **Init** (no IR: your sound passes through):
- **Spaces**: Empty Mall, Pool Rooms, Fluorescent Hall, Backrooms
- **Echoes**: Stairwell Flutter, Tape Corridor, Dream Ping-Pong, Parking Slap, Intercom Echo
- **Strange**: Glass Void, Reverse Bloom, Sub Tunnel, Shimmer Fog, Liminal Hz

The factory IRs are synthetic (generated by `tools/make_factory_irs.py`, not recorded), so they're free to use like
the rest of the plugin.

## My Presets

![My Presets](docs/images/my-presets.png)

Set the plugin the way you like it, then keep it. Tap **MY PRESETS** (beside the PRESET menu); a list opens over the
browser, four presets to a page, with **PREV / NEXT / CLOSE** as in the IR List:
- **SAVE** keeps the current sound: all the knobs and the IR in use (factory or one of yours). It is named after its
  IR ("Fluorescent Hall", "Fluorescent Hall 2"...) and selected.
- Tap a row to load that preset (settings and IR; if its IR has moved it is found again, and if it is gone the
  settings still apply and the line says so). The one in use is outlined, the PRESET field shows "My: <name>", and a
  project remembers which one.
- **DELETE** removes the selected preset: it asks twice, like the IR browser, and deletes the file permanently.
- **NAME ON PHONE**: the MPC's screen has no keyboard, so names are typed on your phone (see below).
- Each preset is a small text file in the plugin's `presets/` folder (`<name>.lhzp`), kept across upgrades. You can
  also rename or delete the files from a computer; press **RESCAN** and the list follows.

## Your phone: TONE3000 and preset names

![The phone pages](docs/images/phone.png)

The plugin serves two small pages to a phone (or computer) on the same Wi-Fi as the MPC, in the plugin's own look.
No app to install, and no password (each closes by itself after 15 idle minutes).
- Tap **BROWSE** and the TONE3000 strip shows an address such as `192.168.1.20:8191`: open it on your phone. Sign in
  to TONE3000 and pick a tone from the **Space, Outboard, Pedal or Experimental** categories; only IRs are offered,
  and only `.wav` IRs are ever downloaded. The tone downloads straight to the MPC, into the plugin's
  `reverbs/TONE3000/<tone>/` folder, and is selected when it's done.
- The same page has a **My Presets: name & save** button: type a name and **Save current sound**, or **Load**,
  **Rename** and **Delete** any preset. **NAME ON PHONE** in the My Presets list opens this page directly (the
  address then ends in `/presets`). The presets page needs no internet.

## Your own IRs

Put `.wav` IRs (mono or stereo, any sample rate) in any folder named `reverbs` on the MPC's drives and cards, in
packs (subfolders) if you like; switch to **MY IRS**. The plugin remembers where it found them, so starting up and
**RESCAN** are quick even on a full card.

## Gen 2 (RK3588, 64-bit)

Gen 2 units (MPC Live III and kin) run a 64-bit system that can't load Gen 1's 32-bit plugins, so each release has two
zips: `Liminal-Hz-<version>-mpc-armv7.zip` (Gen 1) and `Liminal-Hz-<version>-mpc-aarch64.zip` (Gen 2). They are the
same plugin, with the same page, parameters and presets, so a project moves between generations.

| | Gen 1 | Gen 1 with LONG IR | Gen 2 |
|---|---|---|---|
| IR length used | 5 s | **20 s** | **20 s** |
| Decay 300% on a 5 s room | stops at 5 s | **rings ~15 s** | **rings ~15 s** |
| IR resampling (other rates, Stretch) | cubic: -1.9 dB at 17 kHz, Stretch < 100% aliases | **windowed sinc**: flat, aliases -77 dB | **windowed sinc** |
| Decay analysis | STFT 2048 (21.5 Hz bins) | **STFT 4096 (10.8 Hz bins)** | **STFT 4096** |
| Convolution | head (128) + 2048 + far stage (16384) | same | same |
| FFT | NEON | NEON | NEON |

LONG IR is off by default on Gen 1 because 20 s IRs use more of the second core and more memory, and take longer to
prepare after each change. The convolution and the FFT are the same everywhere since 1.3.0 (both made Gen 1 cheaper).
The audio thread's cost doesn't grow with IR length: it always runs the same first 93 ms of the IR (zero latency);
everything later runs on worker threads. 1.2.0 was tested on an MPC Live III and an Akai Force (MPC OS 3.9.1). As for
every Gen 2 plugin today, installing on Gen 2 needs root access on the unit (mpc-vst-plugins docs/GEN2.md).

## Install

**Gen 2**: install `Liminal-Hz-<version>-mpc-aarch64.zip` from the MPC OS Plugin Catalog, or unzip it on the unit and
run its `install.sh` as root (see its INSTALL.md). **Gen 1**: download `Liminal-Hz-for-MPC-OS-<version>.zip` from the releases and follow its README: copy the
`Gm0rb - VST - Liminal Hz` folder into your `Synths` folder and run `vstscanner`; Liminal Hz then appears under
**Gm0rb** in MPC's plugin list. Or install it from the MPC OS Plugin Catalog. After updating, re-insert the plugin on
your tracks. Your `reverbs` and `presets` folders are kept across upgrades.

## How long IRs stay cheap
A 5-second stereo IR is 220,500 taps per channel: convolved in MPC's 128-sample blocks (the usual zero-latency
method, as NAM A2's cab IRs) it would cost far more than a whole audio block on the RK3288. Liminal Hz splits it:
- **head** (the first 4096 samples, 93 ms): 128-sample blocks on the audio thread, zero latency, the same cost every
  block;
- **tail** (the rest): 2048-sample blocks on a second thread, on another core. Output block m of the tail depends on
  input up to block m-2, so each block is computed a whole block (46 ms) ahead of its deadline.

Measured on an Akai Force (RK3288), real time, % of one 2.9 ms audio block:

Measured with 1.2.0 (before the far stage and NEON came to Gen 1):

| 5 s stereo IR | audio thread mean / max | second thread | late blocks |
|---|---|---|---|
| all on the audio thread, 128-sample blocks | too heavy to run | | |
| tail on the audio thread, 2048-sample blocks | 11% / 137% | | 0 |
| **tail on a second thread** (Liminal Hz) | **3.4% / 7.2%** | 8.3% of a core | **0** |

Every method equals direct convolution to float precision (`tools/bench_reverb.cpp --check`, PC and ARM).

1.3.0 on a PC (`tools/bench_reverb --quick`, real time; the worker's load in % of one core, stereo IRs, 0 late blocks):

| IR | tail in 2048 blocks only (Gen 1 up to 1.2.0) | with the far stage (1.3.0) |
|---|---|---|
| 5 s | 1.9% | **0.7%** |
| 10 s | 3.3% | **0.8%** |
| 20 s (LONG IR) | 7.0% | **1.3%** |

Since 1.3.0 (on Gen 2 since 1.2.0), IRs past 0.74 s get a third stage: taps from 32768 on run in 16384-sample blocks on their own worker (each
job has 371 ms to finish). A 20 s IR then costs the audio thread what a 5 s one does, and the 2048-sample worker's job
stays the size it is on Gen 1; the far worker moves most of a long IR's work into a few large, cheap FFTs. Decay,
Feedback's analysis and the playhead's note detection add little on top: the first two run when an IR is prepared,
off the audio thread.

## Build and test
```
git clone https://github.com/sd88me/mpc-vst-plugins ../mpc-vst-plugins
git -C ../mpc-vst-plugins checkout 7c4bf25dd9947eba3f1bf2d8b176bafdcd6a2ae0   # as in .github/workflows/build.yml
pip install ziglang==0.16.0 pillow playwright && python3 -m playwright install --with-deps chromium
sudo apt install qemu-user libc6-armhf-cross libcurl4-openssl-dev
vst/build.sh             # the Gen 1 device plugin (armhf) and its page
vst/build.sh gen2        # the Gen 2 device plugin (aarch64)
vst/build.sh host        # the same plugin for this PC
tools/test.sh            # convolution vs direct, engine, TONE3000 sign-in, library, My Presets, plugin (with a TONE3000 mock)
SANITIZE=asan tools/test.sh ; SANITIZE=tsan tools/test.sh ; ARM=1 tools/test.sh
vst/build.sh host-gen2 && GEN2=1 AARCH64=1 tools/test.sh    # Gen 2: its limits on the PC, the aarch64 build under qemu
sudo apt install libc6-arm64-cross                          # (for AARCH64=1)
REPO=<you>/mpc-vst-liminal-hz tools/package.sh
```
The device benchmark: `tools/bench_reverb.cpp` (see its header). The README images: `tools/make_banners.py` (see its
header). What changed in each version: [CHANGELOG.md](CHANGELOG.md).

## Thanks
To [Amit Talwar](https://github.com/intelliriffer), whose suggestion started the work that made the IR search at start-up and RESCAN fast.

## Support

[![Buy Me a Coffee](https://camo.githubusercontent.com/0cf29a542375e1a46e84d8bf5805a4e5c0a6ee98b6547ccdc0c55eed49d99c69/68747470733a2f2f63646e2e6275796d6561636f666665652e636f6d2f627574746f6e732f76322f64656661756c742d79656c6c6f772e706e67)](https://www.buymeacoffee.com/Gmorb)

## Licence
MIT ([LICENSE](LICENSE)); components and their licences: [NOTICE.md](NOTICE.md).
