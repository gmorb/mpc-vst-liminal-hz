# Self-contained ports: one folder per plugin

How MPC OS plugin ports are packaged so that **everything a plugin needs lives in one folder**, why that works, why
it beats the older install method, and how to do it for every new port.

This applies to Gen 1 Akai Force and MPC standalone units (MPC Live, Live II, One, X, Key 61) running modded
firmware with SSH access (for example MockbaMod), with plugins registered by `vstscanner.sh` from the Force VST
plugins distribution.

---

## The layout

Each plugin ships as a single folder. The folder name is not free-form: it is MPC's own page-folder name,
`<Manufacturer> - VST - <Name>`.

```
Dragonfly - VST - Hall/            <- the whole plugin; copy this one folder
  Hall.so                          the plugin itself (VST2, armhf)
  plugin-meta.xml                  the plugin's entry for MPC's plugin list
  version.xml                      content identifier and version
  Hall.json                        parameter description (names, ranges, units, options)
  Plugin Skins/                    the touchscreen page
    TUI.json                       layout, bindings, fonts
    Q-Links.json, Q-Links - 8by1.json
    *.png                          background, knob and fader filmstrips, list items
  LICENSE                          the plugin's licence
  NOTICE.md                        every component, its authors and licence
  models/, irs/, ...               (optional) data the plugin reads, e.g. amp captures and cabinet IRs
```

A release zip holds one or more of these folders plus collection-level files:

```
Dragonfly Reverb for MPC OS/
  Dragonfly - VST - Hall/
  Dragonfly - VST - Room/
  Dragonfly - VST - Plate/
  Dragonfly - VST - Early Refl/
  README.md  LICENSE  NOTICE.md  licenses/
```

Installing is: copy the plugin folders into a `Synths` folder on any drive under `/media`, then run
`vstscanner`.

---

## How it works

Three pieces fit together.

### 1. The scanner finds the plugin through its `plugin-meta.xml`

`vstscanner.sh` looks for `/media/*/Synths/*/plugin-meta.xml`, meaning every plugin folder sitting **directly** in
a `Synths` folder on any mounted drive: memory cards such as `/media/662522`, and the internal drive
`/media/az01-internal`. It reads each file's `<PLUGIN .../>` line, replaces the placeholder `%payload-path%` with
the path of that `Synths` folder, and writes the result into MPC's plugin list (`pluginList-arm` in
`MPC.settings`). Then it restarts MPC.

`plugin-meta.xml` is one line:

```xml
<PLUGIN name="Hall" descriptiveName="Hall" format="VST" category="Effect" manufacturer="Dragonfly"
        version="1.2.0" file="%payload-path%/Dragonfly - VST - Hall/Hall.so" uid="4466486c" isInstrument="0"
        fileTime="0" infoUpdateTime="0" numInputs="2" numOutputs="2" isShell="0"/>
```

Because the path is relative to the `Synths` folder (`%payload-path%/...`), the same folder works on any card,
under any mount name, without editing anything.

The scanner takes MPC's settings file as its first argument. Without one it uses MockbaMod's
`/data/Settings/MPC/MPC.settings`. On other firmware, pass the path (usually
`/media/az01-internal/Settings/MPC/MPC.settings`).

### 2. MPC finds the page through the folder name

MPC looks for a plugin's touchscreen page in its synth content locations (`SynthContentLocations` in
`MPC.settings`), in a folder named `<manufacturer> - VST - <plugin name>`, and reads `Plugin Skins/` inside it.
When the plugin folder carries exactly that name, **the plugin and its page are the same folder**. There is nothing
to install separately and nothing to keep in sync.

For this to work:
- The folder name must match `manufacturer=` and `name=` in `plugin-meta.xml` **exactly**, including capitals
  and spaces.
- The `Synths` folder the plugin sits in must be one of MPC's content locations. On MockbaMod the card's `Synths`
  folder is. If a plugin shows MPC's plain parameter list instead of its own page, that is the thing to check.

### 3. The plugin finds its own data relative to itself

A plugin that reads files (amp captures, IRs, samples) must never hard-code a path like `/sdcard/...`. It asks the
system where its own `.so` was loaded from and works from there, and it searches `/media` for its data folders. See
"Reading data from the folder and from /media" below.

---

## Why it's better than the old method

The older way to install a port was an `install.sh` that copied the `.so` to `/sdcard/vst/`, copied the page to
`/sdcard/Synths/`, and edited `MPC.settings` by hand.

| | Old: `install.sh` | Self-contained folder |
|---|---|---|
| Install | Run a script that copies files to three places | Copy one folder, run `vstscanner` |
| Remove | Run `uninstall.sh`, hoping it cleans everything | Delete the folder, run `vstscanner` |
| Update | Re-run the installer | Replace the folder |
| Where it lives | Fixed paths (`/sdcard/vst`, `/sdcard/Synths`) | Any `Synths` folder on any drive under `/media` |
| Moving to another card or device | Reinstall | Copy the folder |
| Leftover files | Easy to leave behind (old `.so`, old page) | Impossible: it's one folder |
| Plugin and page out of sync | Possible (update one, not the other) | Impossible: they ship together |
| Many plugins | Each has its own installer | One scan registers them all |
| Enable or disable | Edit settings or uninstall | `vstmanager` (same distribution) |
| Failure modes | Checksum mismatches, wrong paths, partial installs | A folder is either there or not |
| Licences | Often not on the device at all | Travel with the plugin |

In practice:
- **It's what users already do.** The Force VST distribution (AirWindows, DISTRHO and others) uses this layout,
  so a new port drops into the same `Synths` folder next to everything else.
- **Nothing is scattered.** Every file belonging to a plugin is inside its folder, which makes support easy: "send
  me a listing of the folder" answers most questions.
- **No installer to break.** Early Dragonfly releases used `install.sh` and failed on a device with a checksum
  error. Copying a folder has no such step.
- **Portable data.** A plugin that reads files can ship example data inside its own folder, and users can keep
  their own collections on any card.

### One rule: one install method per device

`vstscanner` rebuilds MPC's **whole** plugin list from the folders it finds. A plugin installed the old way (with
an `install.sh` into `/sdcard/vst`) has no folder in a `Synths` directory, so the next scan drops it. On a device
that uses `vstscanner`, install everything as self-contained folders.

A GitHub release can still carry both formats (catalog zips named `*-mpc-armv7.zip` for the MPC OS Plugin
Catalog, and a distribution zip in this layout), because they are separate files. The catalog only reads the
`*-mpc-armv7.zip` ones.

---

## Implementing it for a new port

### Step 1: choose names that stay forever

| What | Rule | Example |
|---|---|---|
| Manufacturer | The vendor or project | `Dragonfly`, `NAM` |
| Plugin name | Short: MPC's insert slot shows about 11 characters | `Hall`, `Early Refl`, `A2 Lite` |
| Folder | `<Manufacturer> - VST - <Name>`, exactly | `Dragonfly - VST - Hall` |
| `.so` file | `<Name>.so` without spaces | `Hall.so`, `EarlyRefl.so` |
| uid | A unique 4-character code, as a hex number | `DfHl` = `4466486c` |
| Content id | lowercase `<vendor>.vst.<name>` | `dragonfly.vst.hall` |

Never change the uid, and avoid renaming things after release. Saved projects find their plugins by them. The
manufacturer line in the insert slot already shows the vendor, so leave it out of the plugin name.

### Step 2: build the plugin and its page

Build the `.so` as usual (VST2, armhf: ARMv7, hard-float, glibc 2.36 or older). Build the page with
mpc-vst-plugins' `gen_vst.py`. It already produces a folder named `<vendor> - VST - <name>` containing
`Plugin Skins/` and a `version.xml`, which is the starting point of the self-contained folder.

Page rule learned the hard way: no image may be taller than **16384 px**. Filmstrips of 128 frames of a tall
control pass that limit easily, and MPC then draws them wrongly (misaligned half-frames). Reduce the frame count
or the frame size.

### Step 3: assemble the folder

Add to the page folder:

1. **The `.so`**, renamed `<Name>.so`.
2. **`plugin-meta.xml`**: the plugin-list entry from `gen_vst.py` (`pluginlist-entry.xml`), with its `file=`
   rewritten to `%payload-path%/<folder>/<Name>.so`.
3. **`version.xml`**, with the release version:

   ```xml
   <?xml version='1.0' encoding='utf-8'?>
   <plugincontent version="1.0">
   	<identifier>dragonfly.vst.hall</identifier>
   	<version>1.2.0.0</version>
   </plugincontent>
   ```
4. **`<Name>.json`**, the parameter description: for each parameter its index, name, range, unit, type and option
   names. The distribution's other plugins ship one, and it documents the plugin for anyone building pages or
   tools later.
5. **`LICENSE` and `NOTICE.md`**: the port's licence and every component's authors and licence. A port is usually
   a derivative work (GPL or MIT upstream), and the notices must travel with the binary.
6. **Data folders** (optional), for example `models/` and `irs/` with a few examples.

The Dragonfly repository automates all of this in `tools/package.sh`. The core of it:

```bash
folder="$vendor - VST - $name"
file="${name// /}.so"                        # Hall.so ... EarlyRefl.so
D="$OUT/$folder"
cp -r "build/skin/$folder" "$D"              # Plugin Skins/ + version.xml from gen_vst.py
cp "build/$so" "$D/$file"
sed "s|file=\"/sdcard/vst/$so\"|file=\"%payload-path%/$folder/$file\"|" \
    build/pluginlist-entry.xml > "$D/plugin-meta.xml"
printf "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>%s</identifier>\n\t<version>%s</version>\n</plugincontent>\n" \
    "$id" "$version4" > "$D/version.xml"
python3 tools/param_json.py "$p" "$D/${name// /}.json"
cp LICENSE NOTICE.md "$D/"
```

### Step 4: test it with the real scanner

Before releasing, run the distribution's `vstscanner.sh` against the folder, either on a device or in a
simulated one (a fake `/media/<card>/Synths` and a copy of `MPC.settings`). Check that:

- the scanner registers the plugin, with the right name, manufacturer and category;
- the registered `file=` path exists, and `Plugin Skins/TUI.json` sits in the same folder;
- the `.so` loads from that path and passes the port's tests;
- running the scanner again changes nothing;
- it also works from a second location (for example `/media/az01-internal/Synths` with the settings path passed
  as the argument).

### Step 5: write the install instructions

Keep them short and give the common case first. For MockbaMod with the card at `/media/662522`:

```
1. Copy the "<Manufacturer> - VST - ..." folder(s) into /media/662522/Synths
2. Run: sh /media/662522/Synths/vstscanner.sh   (afterwards just: vstscanner)
```

Then a section for other firmware, a note on updating (delete the old folder first, then copy the new one), and
the one-install-method rule.

---

## Reading data from the folder and from /media

Plugins that load files (amp captures, IRs, samples, presets) follow two rules: **find your own folder at run time**,
and **search `/media` for data folders** instead of assuming a path.

### Finding the plugin's own folder

The `.so` can ask the system where it was loaded from:

```cpp
#include <dlfcn.h>
#include <string>

// The folder this plugin's .so was loaded from, e.g. "/media/662522/Synths/Gm0rb - VST - A2 Lite".
static std::string plugin_dir() {
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(&plugin_dir), &info) && info.dli_fname) {
    std::string p = info.dli_fname;
    return p.substr(0, p.find_last_of('/'));
  }
  return "";
}
```

(`dladdr` is in glibc's `libc` from 2.34. On older toolchains link with `-ldl`.)

Files shipped inside the folder are then found at `plugin_dir() + "/models"`, wherever the user put the folder.

### Searching /media for data folders

Users keep their collections wherever suits them: a `models` folder at the top of a card, inside a `NAM` folder,
or on the internal drive. The plugin finds them all:

```cpp
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
namespace fs = std::filesystem;

// Every directory named `name` (any case) under `root`, down to `max_depth` levels.
static void find_dirs(const fs::path& root, const std::string& name, int max_depth, std::vector<fs::path>& out) {
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (it.depth() >= max_depth) { it.disable_recursion_pending(); continue; }
    if (!it->is_directory(ec) || it->is_symlink(ec)) continue;   // don't follow links: no loops
    std::string n = it->path().filename().string();
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    if (n == name) { out.push_back(it->path()); it.disable_recursion_pending(); }
  }
}

// e.g. data_dirs("models"): the plugin's own models/ first, then every models/ on the drives under /media
static std::vector<fs::path> data_dirs(const std::string& name) {
  std::vector<fs::path> dirs;
  find_dirs(plugin_dir(), name, 1, dirs);
  find_dirs("/media", name, 4, dirs);
  // (de-duplicate: the plugin's own folder is also under /media)
  std::sort(dirs.begin(), dirs.end());
  dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
  return dirs;
}
```

Rules that keep this safe on a device:
- **Never scan or load on the audio thread.** Scan when the plugin opens, and again on request, on a separate
  thread. Load files there too, and hand the result to the audio thread without locks.
- **Limit the depth** (3–4 levels) and **don't follow symbolic links**, so a scan can't wander through a whole
  card or loop forever.
- **Sort the results** (by folder, then name), so the order is the same every time and a Q-Link position means
  the same file.
- **Save the chosen file by name, not by position**, in the plugin's state. Positions shift when files are added;
  names don't. On load, if the file has moved to another folder, find it again by name.
- **Handle missing files gracefully**: show that the file isn't found and keep the audio running.

---

## Checklist for every new port

- [ ] Manufacturer, name (≤ 11 characters), uid and content id chosen, and recorded as permanent
- [ ] Folder named exactly `<Manufacturer> - VST - <Name>`, matching `plugin-meta.xml`
- [ ] `<Name>.so`, `plugin-meta.xml` (with `%payload-path%`), `version.xml`, `<Name>.json`, `Plugin Skins/`,
      `LICENSE`, `NOTICE.md` inside it
- [ ] No image in `Plugin Skins/` taller than 16384 px
- [ ] No hard-coded paths in the plugin: it finds its own folder with `dladdr` and data under `/media`
- [ ] File scanning and loading happen off the audio thread
- [ ] Tested with the real `vstscanner.sh`: registers, loads from its path, has its page, rescans cleanly,
      works from a second location
- [ ] README: MockbaMod steps first, other firmware second, updating, one install method per device
- [ ] Release zip: `<Collection name>/` with the plugin folder(s), `README.md`, `LICENSE`, `NOTICE.md`,
      `licenses/`
