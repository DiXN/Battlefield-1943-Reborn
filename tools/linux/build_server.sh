#!/usr/bin/env bash
# The Battlefield 1943 Reborn SERVER for Linux from the WSL Linux build, after build_linux.sh and build_appimage.sh
# for the same version (the server's program files come from the game's AppDir):
#
#   BF1943-Reborn-Server-<version>-linux.tar.gz     the plain archive: unpack, run ./bf1943-server (needs python3);
#                                                   also what a running manager downloads to update itself
#   BF1943-Reborn-Server-<version>-x86_64.AppImage  one file with its own Python (TekRantGaming, 13 Nov 2026)
#
#   bash build_server.sh <version> [tar|appimage|all|stage] [output folder]
#
# Names: no file may END with "linux-x86_64.AppImage" but the game's (its updater takes the release file that ends
# so: bf/src/update.cpp; tools/check_package.ps1 refuses a second one).
set -euo pipefail
VERSION="${1:?usage: build_server.sh <version> [tar|appimage|all|stage] [output folder]}"
WHAT="${2:-all}"
BASE="${BF_LINUX_ROOT:-$HOME/bf-linux}"
OUT_DIR="${3:-$BASE/dist}"
WT="${BF_LINUX_SOURCE:-${BF_LINUX_MAIN:-/mnt/f/BF-1942-MP-Decomp}}"
APPDIR="${BF_APPDIR:-$BASE/AppDir-linux-amd64-release}"  # build_appimage.sh made it
TOOL="${BF_APPIMAGETOOL:-/root/kk-linux-beta/work/appimagetool-1.9.1-x86_64.AppImage}"
RT="${BF_APPIMAGE_RUNTIME:-$BASE/runtime-x86_64}"
ONLINE_HOST="${BF_ONLINE_HOST:?set BF_ONLINE_HOST to the main backend address (the official one, or 127.0.0.1 for a local test)}"
NAME="BF1943-Reborn-Server-$VERSION-linux"
STAGE="${BF_SERVER_STAGE:-$BASE/server-stage}"
T="$STAGE/$NAME"

stage() {
  echo "== the server tree ($T)"
  local S="$APPDIR/usr/bin"
  [ -x "$S/bf1943" ] || { echo "no built program at $S (build_linux.sh and build_appimage.sh first)" >&2; exit 1; }
  rm -rf "$STAGE"; mkdir -p "$T/bin" "$T/server/manager_ui" "$T/lib/desktop"
  # The program, its runtime and the plugin (its null GPU on a server), the fonts and the licences. No shader list
  # and no button pictures: a server draws nothing.
  cp -a "$S/bf1943" "$S"/*.so* "$T/bin/"
  if [ -d "$S/fonts" ]; then cp -a "$S/fonts" "$T/bin/"; fi
  if [ -d "$S/licences" ]; then cp -a "$S/licences" "$T/licences"; else echo "warning: no licences folder in $S"; fi
  cp "$WT/server/manager.py" "$WT/server/gmhost.py" "$WT/server/plasma.py" "$WT/server/ed25519.py" "$WT/server/stats.py" "$WT/server/text_filter.py" "$T/server/"
  cp "$WT/server/manager_ui/index.html" "$T/server/manager_ui/"
  cp "$WT/tools/linux/server/bf1943-server" "$T/bf1943-server"
  chmod +x "$T/bf1943-server" "$T/bin/bf1943"
  echo "$VERSION" > "$T/version.txt"
  printf '{"version": 3, "upnp": true, "backend": "%s", "servers": [{"name": "Battlefield 1943 Reborn", "description": "", "maps": ["wake", "guadalcanal", "iwo", "coral"], "round_end_delay": 45, "listed": true, "port": 18300}]}\n' \
    "$ONLINE_HOST" > "$T/server.default.json"
  # The C++ runtime from GCC 13, for systems with an older one, and the X11 and Wayland client libraries the program
  # is linked with, for a host without a desktop (bf1943-server uses each only when the system has none).
  for lib in libstdc++.so.6 libgcc_s.so.1; do cp -L "$(g++ -print-file-name=$lib)" "$T/lib/"; done
  for lib in libX11-xcb.so.1 libX11.so.6 libxcb.so.1 libwayland-client.so.0 libXau.so.6 libXdmcp.so.6 libffi.so.8 libbsd.so.0 libmd.so.0; do
    f="$(ldconfig -p | awk -v l="$lib" '$1==l && /x86-64/ && !seen {print $NF; seen=1}')"
    [ -n "$f" ] || { echo "error: $lib is not on this system" >&2; exit 1; }
    cp -L "$f" "$T/lib/desktop/"
  done
  sed "s/@VERSION@/$VERSION/" "$WT/tools/linux/server/README.txt" > "$T/README.txt"
  sed "s/@VERSION@/$VERSION/" "$WT/tools/linux/server/bf1943-reborn-server.service" > "$T/bf1943-reborn-server.service"
  echo "== Missing libraries (with the bundled ones first)"
  for f in "$T/bin/bf1943" "$T"/bin/*.so*; do
    LD_LIBRARY_PATH="$T/bin:$T/lib/desktop:$T/lib" ldd "$f" 2>/dev/null | grep "not found" | sed "s|^|$(basename "$f"): |"
  done || true
  echo "== No Windows or game files, no key"
  if find "$T" \( -iname "*.dll" -o -iname "*.exe" -o -iname "default.xex" -o -iname "*.key" -o -iname "*.fbrb" \) | grep .; then
    echo "error: the files above must not ship" >&2; exit 1
  fi
}

tarball() {
  mkdir -p "$OUT_DIR"
  local OUT="$OUT_DIR/$NAME.tar.gz"
  tar -C "$STAGE" --owner=0 --group=0 -czf "$OUT" "$NAME"
  echo "$OUT: $(stat -c %s "$OUT") bytes, SHA-256 $(sha256sum "$OUT" | cut -d" " -f1)"
}

# The Python the AppImage carries: this system interpreter, its library (no tests, no Tk, no installers) and the
# shared libraries its modules load that are not part of the C library.
python_into() {
  local P="$1" PY LIBDIR V
  PY="$(readlink -f "$(command -v python3)")"
  LIBDIR="$(python3 -c "import sysconfig; print(sysconfig.get_path('stdlib'))")"
  V="$(basename "$LIBDIR")"  # python3.12
  mkdir -p "$P/bin" "$P/lib/$V"
  cp -L "$PY" "$P/bin/python3"
  rsync -a --exclude test --exclude tests --exclude idle_test --exclude tkinter --exclude idlelib --exclude turtledemo \
    --exclude ensurepip --exclude lib2to3 --exclude pydoc_data --exclude venv --exclude sqlite3 --exclude unittest \
    --exclude __pycache__ --exclude "config-*" --exclude sitecustomize.py --exclude dist-packages --exclude site-packages \
    "$LIBDIR/" "$P/lib/$V/"
  { ldd "$P/bin/python3"; for m in "$P/lib/$V/lib-dynload"/*.so; do ldd "$m"; done; } 2>/dev/null |
    awk '/=> \// {print $3}' | sort -u |
    { grep -Ev "/(libc|libm|libdl|libpthread|librt|libutil|libresolv|libnsl|ld-linux[^/]*)\.so" || true; } |
    while read -r f; do cp -L "$f" "$P/lib/"; done
  LD_LIBRARY_PATH="$P/lib" "$P/bin/python3" -c "import ssl, json, asyncio, zipfile, tarfile, http.server, ctypes, hashlib, lzma, bz2, sys; print('bundled Python', sys.version.split()[0], ssl.OPENSSL_VERSION, sys.prefix)"
}

appimage() {
  local A="$BASE/AppDir-server"
  echo "== AppDir ($A)"
  rm -rf "$A"; mkdir -p "$A"
  cp -a "$T" "$A/tree"
  python_into "$A/tree/python"
  cp "$WT/bf/data/linux/bf1943-reborn.png" "$A/bf1943-reborn-server.png"
  cp "$A/bf1943-reborn-server.png" "$A/.DirIcon"
  cp "$WT/tools/linux/server/bf1943-reborn-server.desktop" "$A/bf1943-reborn-server.desktop"
  cp "$WT/tools/linux/server/AppRun" "$A/AppRun"
  chmod +x "$A/AppRun"
  mkdir -p "$OUT_DIR"
  local OUT="$OUT_DIR/BF1943-Reborn-Server-$VERSION-x86_64.AppImage"
  if [ ! -s "$RT" ] && [ -x "$TOOL" ]; then off="$("$TOOL" --appimage-offset)" && head -c "$off" "$TOOL" > "$RT"; fi
  ARCH=x86_64 "$TOOL" --appimage-extract-and-run ${RT:+--runtime-file "$RT"} "$A" "$OUT" > "$BASE/appimagetool-server.log" 2>&1 ||
    { tail -20 "$BASE/appimagetool-server.log"; exit 1; }
  echo "$OUT: $(stat -c %s "$OUT") bytes, SHA-256 $(sha256sum "$OUT" | cut -d" " -f1)"
}

stage
case "$WHAT" in
  tar) tarball ;;
  appimage) appimage ;;
  all) tarball; appimage ;;
  stage) ;;
  *) echo "usage: build_server.sh <version> [tar|appimage|all|stage] [output folder]" >&2; exit 2 ;;
esac
