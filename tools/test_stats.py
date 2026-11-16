"""The website's totals (server/stats.py) without the game: first the counting on its own (players by their id,
months, hours from server status with its gap limit, the file), then a local backend on 127.0.0.5 (plasma.py --stats)
with a stand in server (registers, reports 4 players twice) and a stand in player (FESL login with an XUID, Theater
USER and EGAM into that server, twice), asked {"op": "stats"}; then the website feed (website/feed) writes stats.json
from that backend. Ports apart from the usual ones; stops everything it started; never touches the live servers.

    python -I tools/test_stats.py              everything (the feed's GitHub download count needs the internet)
    python -I tools/test_stats.py --no-feed    without the feed
"""
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import plasma  # noqa: E402
import stats  # noqa: E402

PY = sys.executable
HOST = '127.0.0.5'
FESL, THEATER, REG = 18541, 18546, 18550
FAILS = []


def check(what, ok):
    print('%s  %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        FAILS.append(what)


def unit():
    t0 = 1794700000.0  # 15 Nov 2026
    s = stats.Stats()
    check('a new player counts', s.joined('2738603114118500', t0) is True)
    check('the same player again does not', s.joined('2738603114118500', t0 + 60) is False)
    check('the XUID counts by its low word (the game\'s player id)', s.joined(2738603114118500 & 0xFFFFFFFF, t0) is False)
    s.joined('2738600816397928', t0)
    check('no id, no player', s.joined('0', t0) is False and s.joined('junk', t0) is False)
    t = s.totals(t0)
    check('two players, two this month', t['players'] == 2 and t['players_month'] == 2 and t['month'] == '2026-11')
    s.joined('2738600816397928', t0 + 30 * 86400)          # December: one of them again
    t = s.totals(t0 + 30 * 86400)
    check('next month: two lifetime, one this month', t['players'] == 2 and t['players_month'] == 1
          and t['month'] == '2026-12')
    s.status('201', 4, t0)
    s.status('201', 4, t0 + 5)        # 4 players for 5 s
    s.status('201', 2, t0 + 10)       # 4 players for 5 s
    s.status('201', 2, t0 + 70)       # a silent minute: counts for GAP seconds only
    check('player seconds from status (gap limited)', abs(s.seconds - (20 + 20 + 2 * stats.GAP)) < 1e-6)
    s.gone('201', t0 + 72)
    check('a server gone adds its last stretch', abs(s.seconds - (40 + 2 * stats.GAP + 4)) < 1e-6 and '201' not in s.live)
    s.seconds = 7200 + 3599
    check('hours are whole hours', s.totals(t0)['hours'] == 2)
    check('the totals hold counts only', set(s.totals(t0)) == {'players', 'players_month', 'month', 'hours', 'since'})
    with tempfile.TemporaryDirectory() as tmp:
        s.path = os.path.join(tmp, 'stats.json')
        s.save(force=True)
        raw = open(s.path, encoding='utf-8').read()
        check('the file keeps no player id as it came', '2738603114118500' not in raw and
              ('%08X' % (2738603114118500 & 0xFFFFFFFF)) not in raw and ('%08x' % (2738603114118500 & 0xFFFFFFFF)) not in raw)
        back = stats.Stats(s.path)
        check('the file reads back', back.load() and back.totals(t0 + 30 * 86400) == s.totals(t0 + 30 * 86400))
        check('a player read back is the same player', back.joined('2738603114118500', t0) is False)
        open(s.path, 'w').write('{not json')
        bad = stats.Stats(s.path)
        check('a broken file: counting starts again, the file kept as .bad', not bad.load()
              and os.path.exists(s.path + '.bad') and bad.totals()['players'] == 0)


def ask(obj, timeout=5):
    with socket.create_connection((HOST, REG), timeout=timeout) as s:
        s.sendall((json.dumps(obj) + '\n').encode())
        return json.loads(s.makefile().readline())


def packet(kind, transmission, fields):
    return plasma.build(kind, transmission, 1, fields)


def read_packet(s):
    head = b''
    while len(head) < 12:
        part = s.recv(12 - len(head))
        if not part:
            raise ConnectionError('closed')
        head += part
    length = struct.unpack('>I', head[8:12])[0]
    body = b''
    while len(body) < length - 12:
        body += s.recv(length - 12 - len(body))
    return head[:4].decode(), plasma.parse_payload(body)


def read_until(s, kind, txn=None):
    for _ in range(20):
        k, f = read_packet(s)
        if k == kind and (txn is None or f.get('TXN') == txn):
            return f
    raise RuntimeError('no %s %s' % (kind, txn or ''))


def wait_port(port, seconds=10):
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection((HOST, port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def player_joins(gid, xuid, name):
    """A stand in player: FESL login with this XUID, then Theater USER with its key and EGAM into the game."""
    with socket.create_connection((HOST, FESL), timeout=10) as fesl:
        fesl.sendall(packet('acct', plasma.REQUEST, {'TXN': 'NuXBL360Login', 'xuid': xuid, 'gamertag': name}))
        lkey = read_until(fesl, 'acct', 'NuXBL360Login')['lkey']
        with socket.create_connection((HOST, THEATER), timeout=10) as th:
            th.sendall(packet('CONN', 0x40000000, {'PROT': '2', 'TID': '1'}))
            read_until(th, 'CONN')
            th.sendall(packet('USER', 0x40000000, {'LKEY': lkey, 'TID': '2'}))
            read_until(th, 'USER')
            th.sendall(packet('EGAM', 0x40000000, {'GID': gid, 'LID': '1', 'PORT': '1000', 'TID': '3'}))
            read_until(th, 'EGAM')


def live(tmp, feed):
    for port in (FESL, THEATER, REG):
        try:
            socket.create_connection((HOST, port), timeout=0.5).close()
        except OSError:
            continue
        check('%s:%d is free (another backend runs there: stop it first)' % (HOST, port), False)
        return
    path = os.path.join(tmp, 'stats.json')
    log = os.path.join(tmp, 'backend.log')
    backend = subprocess.Popen([PY, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST, '--port', str(FESL),
                                '--theater-port', str(THEATER), '--registry-port', str(REG), '--theater-host', HOST,
                                '--login-delay', '0', '--log', log, '--stats', path, '--stats-save-every', '1'],
                               cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    server = None
    try:
        check('the backend starts on %s' % HOST, wait_port(REG) and wait_port(FESL) and wait_port(THEATER))
        server = socket.create_connection((HOST, REG), timeout=10)
        server.sendall((json.dumps({'op': 'register', 'name': 'Stats test', 'port': 18599, 'max_players': 24}) +
                        '\n').encode())
        reader = server.makefile()
        gid = json.loads(reader.readline())['gid']
        check('a stand in server registers (game %s)' % gid, bool(gid))
        server.sendall(b'{"op": "status", "players": 4, "map": "Wake Island", "names": []}\n')
        time.sleep(2.0)
        server.sendall(b'{"op": "status", "players": 4, "map": "Wake Island", "names": []}\n')
        time.sleep(0.3)
        player_joins(gid, '2738603114118500', 'StatsOne')
        player_joins(gid, '2738603114118500', 'StatsOne')   # a rejoin: still one player
        player_joins(gid, '2738600816397928', 'StatsTwo')
        t = ask({'op': 'stats'})
        print('     the backend says: %s' % json.dumps(t))
        check('two players, two this month', t.get('players') == 2 and t.get('players_month') == 2
              and t.get('month') == time.strftime('%Y-%m', time.gmtime()))
        check('the answer holds counts only',
              set(t) == {'op', 'players', 'players_month', 'month', 'hours', 'since'})
        out_dir = os.path.join(tmp, 'site')
        os.makedirs(out_dir)
        if feed:
            r = subprocess.run([PY, '-I', os.path.join(ROOT, 'website', 'feed', 'bf1943_server_feed.py'), '--once',
                                '--host', HOST, '--port', str(REG), '--out', os.path.join(out_dir, 'servers.json'),
                                '--log', os.path.join(tmp, 'feed.log')], cwd=tmp, capture_output=True, text=True,
                               timeout=120)
            check('the feed runs once (exit %d)' % r.returncode, r.returncode == 0)
            doc = json.load(open(os.path.join(out_dir, 'stats.json'), encoding='utf-8'))
            print('     stats.json: %s' % json.dumps(doc))
            check('stats.json has the backend\'s totals', doc.get('players') == 2 and doc.get('players_month') == 2)
            check('stats.json has the GitHub downloads', isinstance(doc.get('downloads'), int) and doc['downloads'] > 0)
            check('stats.json holds no names', 'StatsOne' not in json.dumps(doc) and 'StatsTwo' not in json.dumps(doc))
        server.close()
        server = None
        time.sleep(1.6)   # one save (--stats-save-every 1)
    finally:
        if server:
            server.close()
        backend.terminate()
        backend.wait(10)
    # The backend was stopped hard (as the live one is): what it saved since its start must be in the file.
    back = stats.Stats(path)
    check('the stats file was written', back.load())
    check('it holds the two players', len(back.players) == 2)
    check('it holds the player time from the status reports (4 players for 2 s, then until the server left)',
          back.seconds >= 7.9)
    raw = open(path, encoding='utf-8').read()
    check('it holds no player id or name', '2738603114118500' not in raw and 'StatsOne' not in raw)


def main():
    unit()
    with tempfile.TemporaryDirectory() as tmp:
        live(tmp, '--no-feed' not in sys.argv[1:])
    print('\n%s' % ('all passed' if not FAILS else '%d failed: %s' % (len(FAILS), '; '.join(FAILS))))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
