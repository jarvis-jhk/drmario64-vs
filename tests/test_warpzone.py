"""Online versus over Warpzone: real game processes against a real local warpzone-server.

usage: test_warpzone.py GAME SERVER RELAY_ONLY_SO

Each machine is its own game process with its own Warpzone identity, and a bot plays its
side. Rooms by code: directly, with 15 % of the game datagrams lost, and with every direct
datagram dropped so the relay carries the game. Ranked: two players queue for 1v1, play a
match, both report the same placements and state hash, and the server rates it. In every
case both processes must decide the match at the same tick with the same state hash.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request

GAME, SERVER, RELAY_ONLY = [os.path.abspath(a) for a in sys.argv[1:4]] if len(sys.argv) >= 4 else (None, None, None)
HTTP, UDP = 2997, 3477
DESCRIPTOR = '''id = "drmario64-vs"
name = "Dr. Mario 64 VS"
tagline = "Test descriptor."
tier = 3
[install]
github = "jarvis-jhk/drmario64-vs"
assets = { linux = "drmario64-vs-linux-x86_64.tar.gz", windows = "drmario64-vs-windows-x86_64.zip" }
[launch]
exec = { linux = "drmario64-vs/drmario64-vs", windows = "drmario64-vs/drmario64-vs.exe" }
join = ["--warpzone-join", "{room}"]
host = ["--warpzone-host"]
[rooms]
max_players = 2
presets = [{ id = "1v1", label = "1 vs 1" }]
display = [{ key = "mode", label = "Mode" }, { key = "wins", label = "Wins" }]
[[ladder]]
id = "1v1"
title = "1 vs 1"
teams = 2
'''


class Warpzone(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        root = cls.dir.name
        os.makedirs(os.path.join(root, 'games'))
        with open(os.path.join(root, 'games', 'drmario64-vs.toml'), 'w') as f:
            f.write(DESCRIPTOR)
        with open(os.path.join(root, 'staff.toml'), 'w') as f:
            f.write('moderators = []\nreviewers = []\n')
        with open(os.path.join(root, 'server.toml'), 'w') as f:
            f.write('name = "test"\npublic_url = "http://127.0.0.1:%d"\nhttp_listen = "127.0.0.1:%d"\n'
                    'udp_listen = "127.0.0.1:%d"\ndata_dir = "data"\ngames_dir = "games"\nstaff_file = "staff.toml"\n'
                    % (HTTP, HTTP, UDP))
        cls.log = open(os.path.join(root, 'server.log'), 'w')
        cls.server = subprocess.Popen([SERVER, '--config', 'server.toml'], cwd=root, stdout=cls.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                urllib.request.urlopen('http://127.0.0.1:%d/api/v1/info' % HTTP, timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        cls.homes = [os.path.join(root, 'home%d' % i) for i in range(2)]

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.wait()
        cls.log.close()
        cls.dir.cleanup()

    def env(self, n, **extra):
        e = dict(os.environ, WARPZONE_SERVER='http://127.0.0.1:%d' % HTTP, WARPZONE_HOME=self.homes[n],
                 DM_WZ_NAME='Doctor%d' % n, DM_WZ_VERBOSE='1', DM_NO_REPORT='1',
                 SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        e.update(extra)
        return e

    def finish(self, procs, head=('', '')):
        outs = []
        for p, h in zip(procs, head):
            out, _ = p.communicate(timeout=300)
            outs.append(h + out)
            self.assertEqual(p.returncode, 0, (h + out)[-4000:])
        return outs

    def decided(self, out):
        return re.findall(r'^online match decided tick=(\d+) winner=(\d) hash=(\w+)', out, re.M)

    def room_match(self, relay=False, loss=0, wins=1):
        extra = {'LD_PRELOAD': RELAY_ONLY, 'RELAY_ONLY_PORT': str(UDP)} if relay else {}
        host = subprocess.Popen([GAME, '--headless', '--warpzone-host', '--bot', '--online-exit', '--wins', str(wins)],
                                env=self.env(0, **extra), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        code, lines, deadline = None, [], time.time() + 60
        while code is None and time.time() < deadline:
            line = host.stdout.readline()
            if not line:
                break
            lines.append(line)
            m = re.match(r'warpzone code=([A-Z0-9]{6})', line)
            if m:
                code = m.group(1)
        self.assertIsNotNone(code, ''.join(lines)[-3000:])
        guest = subprocess.Popen([GAME, '--headless', '--warpzone-code', code, '--bot', '--online-exit'],
                                 env=self.env(1, DM_NET_LOSS=str(loss), **extra), stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True)
        outs = self.finish([host, guest], (''.join(lines), ''))
        d = [self.decided(o) for o in outs]
        self.assertEqual(len(d[0]), 1, outs[0][-3000:])
        self.assertEqual(d[0], d[1])
        path = '"relayed":true' if relay else '"relayed":false'
        for o in outs:
            self.assertIn(path, o)
            self.assertIn('online start side=', o)
        print('room wins=%d relay=%s loss=%d%%: decided at tick %s, winner P%s, hash %s' % (wins, relay, loss, *d[0][0]))
        return d[0][0]

    def test_1_room_direct(self):
        self.room_match(wins=3)  # a whole match: first to three rounds

    def test_2_room_lossy(self):
        self.room_match(loss=15)

    def test_3_room_relayed(self):
        self.room_match(relay=True)

    def test_4_ranked_1v1(self):
        procs = [subprocess.Popen([GAME, '--headless', '--ranked', '--bot', '--online-exit'], env=self.env(n, DM_RANKED_WINS='1'),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) for n in range(2)]
        outs = self.finish(procs)
        results = [re.findall(r'^ranked match=(\S+) tick=(\d+) hash=(\w+) winner=(\d) result=(\w+)', o, re.M) for o in outs]
        reports = [re.findall(r'^warpzone report match=\S+ (.*)$', o, re.M) for o in outs]
        for o, r in zip(outs, results):
            self.assertEqual(len(r), 1, o[-3000:])
            self.assertEqual(r[0][4], 'RATED', o[-3000:])
        self.assertEqual(results[0][0], results[1][0])
        self.assertEqual(reports[0], reports[1])
        report = json.loads(reports[0][0])
        print('ranked: match %s decided at tick %s, winner P%s, hash %s, %s; report %s' % (results[0][0][0], results[0][0][1],
              results[0][0][3], results[0][0][2], results[0][0][4], report))


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1], verbosity=2)
