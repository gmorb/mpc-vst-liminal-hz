#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the mpc-vst-liminal-hz contributors
# Package Liminal Hz (run vst/build.sh first): the self-contained plugin folder (Force VST distribution layout,
# docs/SELF_CONTAINED_PORTS.md) and, with REPO=owner/name (GITHUB_REPOSITORY in CI), the MPC OS Plugin Catalog zip.
#   tools/package.sh   -> dist/Liminal-Hz-for-MPC-OS-<VERSION>.zip, dist/Liminal-Hz-<VERSION>-mpc-armv7.zip, SHA256SUMS
#   and, when vst/build.sh gen2 has built vst/build/aarch64/liminal_hz.so, the Gen 2 (MPC Live III and kin, RK3588)
#   zip too: dist/Liminal-Hz-<VERSION>-mpc-aarch64.zip (mpc-vst-plugins docs/GEN2.md: one release, two zips, the same
#   id, uid and parameters). It has release.py's install.sh, which registers under pluginList-arm-64bit.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
MPC_VST="${MPC_VST:-$PWD/../mpc-vst-plugins}"
REPO="${REPO:-${GITHUB_REPOSITORY:-}}"
SOURCE="${REPO:+https://github.com/$REPO (tag v$VERSION)}"
SOURCE="${SOURCE:-the GitHub repository this release was published from (tag v$VERSION)}"
B=vst/build
[ -f "$B/liminal_hz.so" ] || { echo "run vst/build.sh first" >&2; exit 1; }
FOLDER="Gm0rb - VST - Liminal Hz"
COLL="Liminal Hz for MPC OS"
rm -rf dist; mkdir -p dist/work
python3 tools/param_json.py dist/work/LiminalHz.json

# 1. the plugin folder, built by mpc-vst-plugins' release.py (its portable layout, docs/CATALOG_SPEC.md): the .so,
#    plugin-meta.xml, version.xml and the page, plus our extras; models/ and irs/ are the user's folders (kept by the
#    installer across upgrades and uninstalls)
REL_ARGS=(--so "$B/liminal_hz.so" --skin "$B/skin/$FOLDER" --entry "$B/pluginlist-entry.xml" --version "$VERSION"
          --id liminal-hz --license MIT
          --about "$(python3 -c "import json;print(json.load(open('vst/vst.json'))['about'])")"
          --extra "LICENSE:LICENSE" --extra "NOTICE.md:NOTICE.md" --extra "dist/work/LiminalHz.json:LiminalHz.json"
          --extra "packaging/reverbs-README.txt:reverbs/README.txt"
          --extra "packaging/presets-README.txt:presets/README.txt"
          --extra "vst/art/tone3000-logo.png:art/tone3000-logo.png"
          --extra "$MPC_VST/tools/html_art/fonts/TitilliumWeb-Regular.ttf:art/fonts/TitilliumWeb-Regular.ttf"
          --extra "$MPC_VST/tools/html_art/fonts/TitilliumWeb-SemiBold.ttf:art/fonts/TitilliumWeb-SemiBold.ttf"
          --extra "$MPC_VST/tools/html_art/fonts/OFL.txt:art/fonts/OFL.txt"
          --user-data reverbs --user-data presets)
[ -n "$REPO" ] && REL_ARGS+=(--repo "$REPO")
# the factory IRs (tools/make_factory_irs.py): factory/<category>/<name>.wav in the plugin folder (not user data:
# an upgrade brings the current ones)
[ -n "$(ls packaging/factory/*/*.wav 2>/dev/null)" ] || { echo "error: no factory IRs (run tools/make_factory_irs.py)" >&2; exit 1; }
while IFS= read -r f; do REL_ARGS+=(--extra "$f:${f#packaging/}"); done < <(find packaging/factory -name '*.wav' | sort)
python3 "$MPC_VST/tools/release.py" "${REL_ARGS[@]}" -o dist/work >/dev/null
CZ=$(ls dist/work/*-mpc-armv7.zip)

# 2. the Force VST distribution zip: the same plugin folder, with the collection's README and licences
mkdir -p "dist/stage/$COLL"
(cd dist/work && unzip -q "$(basename "$CZ")")
cp -r dist/work/*/portable/"$FOLDER" "dist/stage/$COLL/"
C="dist/stage/$COLL"
cp LICENSE NOTICE.md "$C/"
mkdir -p "$C/licenses"
cp engine/pffft/LICENSE.txt "$C/licenses/PFFFT.txt"
cp src/audiodsptools/LICENSE "$C/licenses/AudioDSPTools-MIT.txt"
sed -e "s|@VERSION@|$VERSION|" -e "s|@SOURCE@|$SOURCE|" packaging/README.md > "$C/README.md"
find dist/stage -exec touch -d "2026-01-01 00:00:00" {} +
(cd dist/stage && find "$COLL" -type f | LC_ALL=C sort | zip -q -X -@ "../Liminal-Hz-for-MPC-OS-$VERSION.zip")
rm -rf dist/stage

# 3. Gen 2 (aarch64): the same plugin folder with the 64-bit .so and the Gen 2 limits (engine/platform.h)
if [ -f "$B/aarch64/liminal_hz.so" ]; then
  mkdir -p dist/work64
  sed -e 's/up to 5 seconds are used in full (20 seconds with LONG IR on)/up to 20 seconds are used in full (Gen 2: LONG IR is always on)/' \
      packaging/reverbs-README.txt > dist/work64/reverbs-README.txt
  grep -q "20 seconds" dist/work64/reverbs-README.txt || { echo "error: reverbs-README.txt changed; update the Gen 2 sed" >&2; exit 1; }
  ABOUT64="$(python3 -c "import json;print(json.load(open('vst/vst.json'))['about'].replace('up to 5 seconds (20 with LONG IR on)', 'up to 20 seconds'))") Gen 2 build (RK3588, 64-bit)."
  A64=()
  for ((i = 0; i < ${#REL_ARGS[@]}; i++)); do
    case "${REL_ARGS[$i]}" in
      --so) A64+=(--so "$B/aarch64/liminal_hz.so"); i=$((i + 1)) ;;
      --about) A64+=(--about "$ABOUT64"); i=$((i + 1)) ;;
      --extra) case "${REL_ARGS[$((i + 1))]}" in
                 packaging/reverbs-README.txt:*) A64+=(--extra "dist/work64/reverbs-README.txt:reverbs/README.txt") ;;
                 *) A64+=(--extra "${REL_ARGS[$((i + 1))]}") ;;
               esac; i=$((i + 1)) ;;
      *) A64+=("${REL_ARGS[$i]}") ;;
    esac
  done
  python3 "$MPC_VST/tools/release.py" "${A64[@]}" -o dist/work64 >/dev/null
  mv dist/work64/*-mpc-aarch64.zip dist/
  rm -rf dist/work64
  echo "Gen 2 zip: $(ls dist/*-mpc-aarch64.zip)"
fi

# 4. the catalog zip, only with a source repo (the catalog needs it in the manifest)
if [ -n "$REPO" ]; then
  mv "$CZ" dist/
  for z in dist/*-mpc-armv7.zip dist/*-mpc-aarch64.zip; do
    [ -f "$z" ] || continue
    python3 "$MPC_VST/tools/catalog_check.py" "$z" --catalog >/dev/null || { python3 "$MPC_VST/tools/catalog_check.py" "$z" --catalog >&2; exit 1; }
    echo "catalog zip ok: $z"
  done
else
  echo "no REPO / GITHUB_REPOSITORY: catalog zip skipped (set REPO=owner/name)"
fi
rm -rf dist/work
(cd dist && sha256sum *.zip > SHA256SUMS)
awk -v v="$VERSION" '/^## /{on = ($2 == v)} on && !/^## /' CHANGELOG.md > dist/RELEASE_NOTES.md
ls -la dist
