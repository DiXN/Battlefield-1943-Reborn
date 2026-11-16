"""Checks for server/player_stats.py and the backend's ops for it (TekRantGaming, 16 Nov 2026: Stats & Rewards,
Leaderboards, Achievements). Starts a backend on 127.0.0.5 (no real server or player is touched), registers a fake
dedicated server, sends its counts and reads the boards and records back.
    python -I tools/test_player_stats.py
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import player_stats  # noqa: E402

failed = []


def check(what, ok):
    print(('ok    ' if ok else 'FAIL  ') + what)
    if not ok:
        failed.append(what)


# ---- the module alone
tmp = tempfile.mkdtemp(prefix='bf_player_stats_')
path = os.path.join(tmp, 'player_stats.json')
ps = player_stats.PlayerStats(path)
check('a row counts', ps.add([{'name': 'Able', 'kills': 10, 'deaths': 4, 'score': 300, 'seconds': 600, 'map': 'wake',
                               'rounds': 1, 'wins': 1, 'streak': 5}]) == 1)
check('a bad count drops the row', ps.add([{'name': 'Baker', 'kills': -1}]) == 0)
check('a huge step drops the row', ps.add([{'name': 'Baker', 'kills': 10 ** 9}]) == 0)
check('a float drops the row', ps.add([{'name': 'Baker', 'kills': 1.5}]) == 0)
check('a name is cleaned', ps.add([{'name': 'Ba"ker<>', 'kills': 2, 'deaths': 2, 'score': 50}]) == 1 and 'baker' in ps.rows)
ps.add([{'name': 'Able', 'kills': 5, 'deaths': 1, 'streak': 3, 'map': 'wake', 'rounds': 1, 'losses': 1}])
t = ps.totals('Able')
check('counts add up', t['kills'] == 15 and t['deaths'] == 5 and t['rounds'] == 2)
check('the best streak is kept', ps.rows['able']['best_streak'] == 5)
check('by map', ps.rows['able']['maps']['wake'] == {'rounds': 2, 'wins': 1, 'kills': 15, 'deaths': 5})
check('this month and this week', ps.totals('Able', 'month')['kills'] == 15 and ps.totals('Able', 'week')['kills'] == 15)
b = ps.board('kills', 'all', 50, 'Baker')
check('a board by kills', [r['name'] for r in b['rows']] == ['Able', 'Baker'] and b['rows'][0]['kills'] == 15)
check('your own row and place', b.get('me', {}).get('place') == 2)
check('K/D needs 50 kills', ps.board('kd')['total'] == 0)
ps.add([{'name': 'Able', 'kills': 40, 'deaths': 10}])
check('K/D board', ps.board('kd')['rows'][0]['kd'] == round(55 / 15, 2))
fb = {'charlie': {'name': 'Charlie', 'score': 9999, 'seconds': 100, 'captures': 3}}
b = ps.board('score', 'all', 50, '', fb)
check("a player's own totals fill in", b['rows'][0]['name'] == 'Charlie' and b['total'] == 3)
check('not in a month board', ps.board('score', 'month', 50, '', fb)['total'] == 2)
c = ps.card('able', fb)
check('a card', c['found'] and c['kills'] == 55 and c['places']['kills'] == [1, 2])
ps.rename('Able', 'Alpha')
check('a rename moves the stats', 'alpha' in ps.rows and 'able' not in ps.rows)
ps.save(force=True)
ps2 = player_stats.PlayerStats(path)
check('saved and read back', ps2.totals('Alpha')['kills'] == 55)
old = dict(ps2.rows['alpha']['week'])
ps2.rows['alpha']['week']['id'] = '2000-W01'
check('an old week reads as nothing', ps2.totals('Alpha', 'week')['kills'] == 0)
ps2.rows['alpha']['week'] = old

# ---- the backend: a server's counts, the boards, a card
HOST, PORT = '127.0.0.5', 18299


def ask(sock_file, m):
    sock_file.write((json.dumps(m) + '\n').encode())
    sock_file.flush()
    return json.loads(sock_file.readline())


def one(m):
    with socket.create_connection((HOST, PORT), timeout=5) as s:
        f = s.makefile('rwb')
        return ask(f, m)


for trusted in ('all', 'official'):
    stats_file = os.path.join(tmp, 'backend_%s.json' % trusted)
    log = os.path.join(tmp, 'plasma_%s.log' % trusted)
    proc = subprocess.Popen([sys.executable, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST,
                             '--port', '18290', '--theater-port', '18291', '--registry-port', str(PORT),
                             '--player-stats', stats_file, '--player-stats-from', trusted, '--log', log],
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(50):
            try:
                socket.create_connection((HOST, PORT), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        with socket.create_connection((HOST, PORT), timeout=5) as s:
            f = s.makefile('rwb')
            reg = ask(f, {'op': 'register', 'name': 'Test server', 'port': 18300, 'listed': False})
            f.write((json.dumps({'op': 'player_stats', 'rows': [
                {'name': 'Dog', 'kills': 7, 'deaths': 3, 'score': 210, 'seconds': 300, 'captures': 2, 'map': 'iwo',
                 'rounds': 1, 'wins': 1, 'kit_scout': 300, 'veh_plane': 60, 'vk_plane': 2}]}) + '\n').encode())
            f.flush()
            time.sleep(0.3)
            board = one({'op': 'leaderboard', 'sort': 'kills', 'period': 'week', 'me': 'Dog'})
            if trusted == 'all':
                check('backend: a server counts (test backend, every server)', board['total'] == 1 and board['rows'][0]['kills'] == 7)
                check('backend: your own row', board.get('me', {}).get('place') == 1)
                card = one({'op': 'player_card', 'name': 'dog'})
                check('backend: a card', card['found'] and card['vk_plane'] == 2 and card['maps']['iwo']['wins'] == 1)
                old = one({'op': 'leaderboard'})
                check('backend: the old leaderboard still answers', old['op'] == 'leaderboard' and 'rows' in old)
                share = one({'op': 'achievement_share'})
                check('backend: achievement share', share['op'] == 'achievement_share' and 'have' in share)
            else:
                check('backend: a community server does not count', board['total'] == 0)
    finally:
        proc.stdin.close()
        proc.terminate()
        proc.wait(5)

print('\n%d failed' % len(failed) if failed else '\nall passed')
sys.exit(1 if failed else 0)
