#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
DM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$DM_DIR/tools/linux/launcher.py" "$@"
