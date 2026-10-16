#!/usr/bin/env python3
# Copyright (C) 2026 s25coop contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Steam writes shortcuts on exit. Only artwork may be changed while it is running.
# Installer originally based on ArnoldSmith86/minecraft-splitscreen (MIT).
import glob
import shutil
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ARTWORK = {suffix: Path(__file__).resolve().parents[2] / "artwork" / ("steam" + suffix)
           for suffix in ("p.png", ".png", "_hero.png", "_logo.png", "_icon.png")}



def cstring(data, pos):
    end = data.index(b'\0', pos)
    return data[pos:end], end + 1


def fields(data, pos=0):
    """Parse field boundaries so a path/name in another field cannot select a shortcut."""
    result = []
    while pos < len(data):
        start = pos
        kind = data[pos]
        pos += 1
        if kind == 8:
            return result, pos
        key, pos = cstring(data, pos)
        value_start = pos
        if kind == 0:
            value, pos = fields(data, pos)
        elif kind == 1:
            value, pos = cstring(data, pos)
        elif kind in (2, 3, 4, 6, 7, 10):
            size = 8 if kind in (7, 10) else 4
            if pos + size > len(data):
                raise ValueError('truncated numeric field')
            value = data[pos:pos + size]
            pos += size
        else:
            raise ValueError('unsupported binary VDF field type: %d' % kind)
        result.append((kind, key, start, pos, value_start, value))
    raise ValueError('unterminated binary VDF object')


def shortcuts(data):
    top, end = fields(data)
    if end != len(data) or len(top) != 1 or top[0][:2] != (0, b'shortcuts'):
        raise ValueError('unrecognised shortcuts.vdf root')
    return top[0]


def matching_entry(root, exe):
    matches = []
    for node in root[5]:
        if node[0] != 0:
            raise ValueError('invalid shortcut entry')
        values = {field[1].lower(): field for field in node[5]}
        executable = values.get(b'exe')
        if executable and executable[0] == 1 and executable[5].strip(b'"') == exe.encode():
            appid = values.get(b'appid')
            if not appid or appid[0] != 2:
                raise ValueError('shortcut has no valid appid')
            matches.append((node, values, struct.unpack('<I', appid[5])[0]))
    if len(matches) > 1:
        raise ValueError('multiple shortcuts for this executable')
    return matches[0] if matches else None


def string_field(key, value):
    return b'\x01' + key + b'\0' + value.encode() + b'\0'


def new_entry(index, appid, name, exe, startdir, options, icon):
    return (b'\0' + str(index).encode() + b'\0\x02appid\0' + struct.pack('<I', appid)
            + string_field(b'AppName', name) + string_field(b'Exe', '"%s"' % exe)
            + string_field(b'StartDir', '"%s"' % startdir) + string_field(b'icon', icon)
            + string_field(b'LaunchOptions', options) + b'\x08')


def renamed(data, match, name, icon):
    node, values, _ = match
    edits = []
    for key, value in ((b'appname', name), (b'icon', icon)):
        field = values.get(key)
        if field:
            if field[0] != 1:
                raise ValueError('shortcut name/icon is not a string')
            edits.append((field[4], field[3], value.encode() + b'\0'))
        else:
            edits.append((node[3] - 1, node[3] - 1, string_field(key, value)))
    for start, end, replacement in sorted(edits, reverse=True):
        data = data[:start] + replacement + data[end:]
    return data


def steam_closed():
    # Refuse an unknown process state too; failing to inspect Steam is not proof it exited.
    result = subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL, check=False)
    if result.returncode != 1:
        raise RuntimeError('Steam is running or its process state could not be checked')


def replace_shortcuts(path, data):
    steam_closed()
    backup = path + '.drmario64-vs-backup'
    if os.path.exists(path) and not os.path.exists(backup):
        with open(path, 'rb') as src, open(backup, 'xb') as dst:
            dst.write(src.read())
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=os.path.dirname(path), delete=False) as out:
            temporary = out.name
            if os.path.exists(path):
                os.chmod(temporary, os.stat(path).st_mode & 0o777)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        steam_closed()
        os.replace(temporary, path)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)


def fetch_artwork(grid, appid):
    os.makedirs(grid, exist_ok=True)
    failed = False
    for suffix, source in ARTWORK.items():
        path = os.path.join(grid, '%d%s' % (appid, suffix))
        if os.path.exists(path):
            continue
        try:
            shutil.copyfile(source, path + '.part')
            os.replace(path + '.part', path)
            print('artwork', path)
        except OSError as error:
            failed = True
            print('artwork failed: %s: %s' % (source, error), file=sys.stderr)

    if failed:
        raise RuntimeError("Steam artwork could not be installed; rerun the installer")


def main(args):
    mode = args.pop(0) if args and args[0].startswith('--') else 'add'
    if mode not in ('add', '--artwork-only', '--status', '--migrate-name'):
        raise ValueError('unknown mode')
    if len(args) != (4 if mode == 'add' else 2):
        raise ValueError('usage: [--status|--migrate-name|--artwork-only] <name> <exe> [<start dir> <options>]')
    name, exe = args[:2]
    configs = sorted(glob.glob(os.path.expanduser('~/.steam/steam/userdata/*/config')))
    if not configs:
        raise ValueError('no Steam user found')
    plans = []
    states = []
    for config in configs:
        path = os.path.join(config, 'shortcuts.vdf')
        grid = os.path.join(config, 'grid')
        data = b'\x00shortcuts\x00\x08\x08'
        if os.path.exists(path):
            with open(path, 'rb') as src:
                data = src.read()
        root = shortcuts(data)
        match = matching_entry(root, exe)
        appid = match[2] if match else None
        title = match[1].get(b'appname') if match else None
        states.append('missing' if not match else 'current' if title and title[5] == name.encode() else 'old')
        updated = data
        if mode in ('add', '--migrate-name') and match:
            updated = renamed(data, match, name, os.path.join(grid, '%d_icon.png' % appid))
        elif mode == 'add' and not match:
            appid = (zlib.crc32((exe + name).encode()) & 0xFFFFFFFF) | 0x80000000
            indices = [int(node[1]) for node in root[5]]
            position = root[3] - 1
            entry = new_entry(max(indices, default=-1) + 1, appid, name, exe, args[2], args[3],
                              os.path.join(grid, '%d_icon.png' % appid))
            updated = data[:position] + entry + data[position:]
        plans.append((path, grid, appid, data, updated))
    if mode == '--status':
        print('old' if 'old' in states else 'missing' if 'missing' in states else 'current')
        return
    # Validate all users before editing any, and guard independently of the shell caller.
    if mode != '--artwork-only':
        steam_closed()
    for path, grid, appid, data, updated in plans:
        if updated != data:
            replace_shortcuts(path, updated)
            print('updated', path)
        # Launch migration is synchronous; artwork stays in the separate background call.
        if appid is not None and mode != '--migrate-name':
            fetch_artwork(grid, appid)


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit('Steam shortcuts: ' + str(error))
