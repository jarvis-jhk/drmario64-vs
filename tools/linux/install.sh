#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
DM_TAG="@TAG@"
dialog() {
    local kind=$1 text=$2
    if command -v zenity >/dev/null; then zenity --"$kind" --no-wrap --text="$text"
    elif command -v kdialog >/dev/null; then
        case "$kind" in question) kdialog --yesno "$text";; error) kdialog --error "$text";; *) kdialog --msgbox "$text";; esac
    elif [ "$kind" = question ]; then
        local answer
        read -rp "$text [y/N] " answer && [[ "$answer" == [yY]* ]]
    else printf '%s\n' "$text"; fi
}
fault() {
    # Only a fixed stage name leaves the device, never paths or diagnostic output.
    if [ "${DM_NO_REPORT:-0}" != 1 ]; then curl -fsS --max-time 8 -H 'Title: drmario64-vs installer fault' \
        --data-binary "drmario64-vs $DM_TAG Linux: installer $1" \
        https://ntfy.sh/drmario64-vs-faults-9c41d7a2e05b4f18 >/dev/null 2>&1 &
    fi
    dialog error "Installation failed ($1). Please try again." || true
    exit 1
}
for tool in python3 curl; do command -v "$tool" >/dev/null || { dialog error "Install $tool first"; exit 1; }; done
dialog question 'Install Dr. Mario 64 VS, the unofficial two-player remake? This installs the game, updates, menu entry and Steam artwork.' || exit 0
DM_TMP=$(mktemp -d)
trap 'rm -rf "$DM_TMP"' EXIT
# Bootstrap from the same release as this installer, not an unreviewed moving branch.
curl -fsSL --max-time 60 "https://github.com/jarvis-jhk/drmario64-vs/releases/download/$DM_TAG/launcher.py" -o "$DM_TMP/launcher.py" || fault download
python3 "$DM_TMP/launcher.py" --install || fault bootstrap
DM_ROOT="$HOME/.local/share/drmario64-vs/current"
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/drmario64-vs.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Dr. Mario 64 VS
Comment=Two-player local versus, frame-exact like Dr. Mario 64
Exec="$DM_ROOT/drmario64-vs.sh"
Icon=$DM_ROOT/artwork/steam_icon.png
Terminal=false
Categories=Game;
DESKTOP
if pgrep -x steam >/dev/null; then
    dialog question 'The game is installed. Fully exit Steam now, then click Yes to add or update its shortcut. No skips adding it.' || {
        dialog info 'Installed in your applications menu. Run this installer again with Steam closed to add the Steam shortcut.'
        exit 0
    }
fi
if python3 "$DM_ROOT/tools/linux/add-to-steam.py" 'Dr. Mario 64 VS' "$DM_ROOT/drmario64-vs.sh" "$DM_ROOT" ''; then
    dialog info 'Installed with Steam artwork. Start Steam again and return to Game Mode. Use the Gamepad template. For sound effects, drop your own Dr. Mario 64 (USA) ROM onto the game window once.'
else
    dialog error 'The game is installed in your applications menu. Steam shortcut could not be added: fully exit Steam and run this installer again, or add the game manually.'
fi
# END OF DRMARIO64-VS INSTALLER
