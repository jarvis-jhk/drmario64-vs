#!/bin/bash
# Copyright (C) 2026 s25coop contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Turns a `cmake --install` tree into a portable folder that runs on SteamOS and Linux Mint:
# copies every shared library the binaries need into lib/, except the ones that belong to the
# host (glibc, GPU/X11/Wayland/audio stacks and what they link: glib, libsndfile, …) — bundling
# those breaks graphics and sound, because the host PulseAudio/PipeWire would load our older copies.
set -euo pipefail

PREFIX=$(cd "$1" && pwd)
LIBDIR="$PREFIX/lib"
mkdir -p "$LIBDIR"

# Host libraries: must come from the system (same list idea as AppImage's excludelist).
EXCLUDE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libnsl|libanl|libstdc\+\+|libgcc_s|libGL.*|libEGL|libGLX.*|libGLdispatch|libOpenGL|libdrm|libgbm|libvulkan|libX.*|libxcb.*|libxkbcommon.*|libwayland-.*|libdecor.*|libasound|libpulse.*|libpipewire.*|libjack|libdbus-1|libudev|libsystemd|libz|libexpat|libfontconfig|libfreetype|libharfbuzz|libgmp|libcap|libselinux|libapparmor|libglib-2\.0|libgobject-2\.0|libgmodule-2\.0|libgio-2\.0|libgthread-2\.0|libffi|libpcre.*|libsndfile|libasyncns|libgcrypt|libgpg-error|liblz4|liblzma|libzstd|libbsd|libmd|libsystemd)\.so'

mapfile -t ELF < <(find "$PREFIX" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'head -c4 "$1" | grep -q ELF' _ {} \; -print)
declare -A seen=()
for f in "${ELF[@]}"; do
    while read -r name _ path _; do
        [[ "$path" == /* ]] || continue
        [[ "$name" =~ $EXCLUDE ]] && continue
        # ldd includes dependencies of host libraries too. Copy only SDL's portable
        # runtime; host audio codecs must remain with host libsndfile.
        [[ "$name" =~ ^libSDL2.*\.so ]] || continue
        [[ -n "${seen[$name]:-}" ]] && continue
        seen[$name]=1
        cp -Lv "$path" "$LIBDIR/$name"
    done < <(ldd "$f" | grep '=>')
done
# Bundled libs find each other through the launcher's LD_LIBRARY_PATH.
strip --strip-unneeded "$LIBDIR"/*.so* 2>/dev/null || true
echo "Bundled ${#seen[@]} libraries."
