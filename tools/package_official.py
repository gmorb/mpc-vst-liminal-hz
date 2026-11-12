#!/usr/bin/env python3
"""package_official.py - the release zip for MPC OS 3.10.1 and later (Akai's official route, no install script).

    tools/package_official.py VERSION ARMV7_ZIP AARCH64_ZIP OUT_ZIP

MPC OS 3.10.1 loads third-party plugins from a folder named "VST2 Plugins" at the root of any drive, once
Preferences > Enhancements (Beta) > Enable Enhancements and External Plugin Support are on (Akai's MPC Plugin SDK,
inmusic-software/mpc-plugin-builder-sdk, docs/plugin-folder-format.md). One folder serves both generations:

    Gm0rb - VST - Liminal Hz/
        plugin.json          name, manufacturer and uid as the binary reports them; file32 / file64
        version.xml          with <uiversion>0.1.0.0</uiversion>, or the MPC may ignore the Plugin Skins
        liminal-hz-arm32.so  liminal-hz-arm64.so   (beside the data: the plugin finds factory/, presets/, art/ and its
                                                    folder index next to its own .so, Library::plugin_dir)
        Plugin Skins/  factory/  art/  presets/  reverbs/  LICENSE  NOTICE.md

It is made from the two community release zips (their portable/ folder), so the binaries are the very same ones.
The community installer's own files (plugin-meta.xml, mpc-plugin.json) are left out. Deterministic: sorted entries,
fixed timestamps. Standard library only.
"""

import io
import json
import sys
import zipfile

FOLDER = "Gm0rb - VST - Liminal Hz"
SKIP = {"plugin-meta.xml", "mpc-plugin.json", "version.xml", "liminal_hz.so"}
STAMP = (2026, 1, 1, 0, 0, 0)

INSTALL = """Liminal Hz {v} for MPC OS 3.10.1 and later
==========================================

Works on every MPC and Force that runs MPC OS 3.10.1 or later, Gen 1 (MPC Live, Live II, One, One+, X, X SE,
Key 61, Key 37, Force) and Gen 2 (MPC Live III, MPC XL): this one folder holds both builds.

1. Update the MPC to MPC OS 3.10.1 or later (Preferences > Info shows the version).
2. On the MPC: Preferences > Enhancements (Beta): turn on Enable Enhancements and External Plugin Support.
   Read the warning: third-party plugins are not tested or supported by Akai. Save your work often.
3. On a USB drive or SD card, make a folder named exactly  VST2 Plugins  at the root (if it isn't there), and copy
   the folder  "{f}"  into it.
4. Put the drive in the MPC. Add an insert effect to a track: Liminal Hz is under External Plugins.

Your own IRs: put .wav files in any folder named  reverbs  on your drives (see the README).
Updating: copy the new folder over the old one, then remove and re-add the plugin on your tracks.
Uninstall: delete the folder from VST2 Plugins.

Already installed Liminal Hz the older way (the Plugin Catalog, or install.sh)? Use one or the other, not both.
Manual and source: https://github.com/gmorb/mpc-vst-liminal-hz
"""


def portable(zpath):
    """{relative path: bytes} of the plugin folder inside a community release zip."""
    out = {}
    with zipfile.ZipFile(zpath) as z:
        for info in z.infolist():
            parts = info.filename.split("/")
            if info.is_dir() or "portable" not in parts:
                continue
            i = parts.index("portable")
            if len(parts) < i + 3 or parts[i + 1] != FOLDER:
                continue
            out["/".join(parts[i + 2:])] = z.read(info)
    if "liminal_hz.so" not in out:
        sys.exit("%s: no portable/%s/liminal_hz.so" % (zpath, FOLDER))
    return out


def elf_machine(b):
    if b[:4] != b"\x7fELF":
        sys.exit("not an ELF binary")
    return int.from_bytes(b[18:20], "little")


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    version, z32, z64, out = sys.argv[1:]
    a, b = portable(z32), portable(z64)
    if elf_machine(a["liminal_hz.so"]) != 0x28 or elf_machine(b["liminal_hz.so"]) != 0xB7:
        sys.exit("expected an armv7 zip, then an aarch64 zip")
    files = {k: v for k, v in a.items() if k not in SKIP}
    # the reverbs/ readme says how long an IR may be, per generation: one text for both
    gen1 = b"IRs up to 5 seconds are used in full (20 seconds with LONG IR on); longer ones are cut there (faded out)."
    both = (b"IRs up to 5 seconds are used in full on Gen 1 (20 seconds with LONG IR on) and up to 20 seconds on Gen 2; "
            b"longer ones are cut there (faded out).")
    rd = "reverbs/README.txt"
    if rd in files:
        if gen1 not in files[rd]:
            sys.exit("%s: the IR length line changed; update package_official.py" % rd)
        files[rd] = files[rd].replace(gen1, both)
    for k, v in b.items():               # the two zips carry the same data; only the binary differs
        if k not in SKIP and k != rd and files.get(k, v) != v:
            sys.exit("%s differs between the two zips" % k)
    files["liminal-hz-arm32.so"] = a["liminal_hz.so"]
    files["liminal-hz-arm64.so"] = b["liminal_hz.so"]
    files["plugin.json"] = (json.dumps({
        "name": "Liminal Hz",
        "manufacturer": "Gm0rb",
        "version": version,
        "format": "vst2",
        "kind": "effect",
        "uid": "NmIR",
        "category": "Effect",
        "numInputs": 2,
        "numOutputs": 2,
        "file32": "liminal-hz-arm32.so",
        "file64": "liminal-hz-arm64.so",
        "description": "Convolution reverb: rooms, echoes, strange spaces and textures from impulse responses, "
                       "up to 20 s; your own IRs and TONE3000.",
        "author": "Gm0rb",
        "url": "https://github.com/gmorb/mpc-vst-liminal-hz",
        "license": "MIT",
    }, indent=2) + "\n").encode()
    v4 = ".".join((version.split(".") + ["0"] * 4)[:4])
    files["version.xml"] = ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<plugincontent version=\"1.0\">\n"
                            "\t<identifier>Gm0rb.vst.liminalhz</identifier>\n\t<version>%s</version>\n"
                            "\t<uiversion>0.1.0.0</uiversion>\n</plugincontent>\n" % v4).encode()
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        def put(name, data, mode=0o644):
            info = zipfile.ZipInfo(name, STAMP)
            info.external_attr = mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
        put("INSTALL.txt", INSTALL.format(v=version, f=FOLDER).encode())
        for k in sorted(files):
            put("%s/%s" % (FOLDER, k), files[k], 0o755 if k.endswith(".so") else 0o644)
    open(out, "wb").write(buf.getvalue())
    print("official zip ok: %s (%d files)" % (out, len(files)))


if __name__ == "__main__":
    main()
