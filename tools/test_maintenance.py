"""Maintenance mode and the refusal messages without the game (server/plasma.py maintenance_now, server/manager.py
maintenance): a backend of its own on 127.0.0.5 (ports 18541, 18546, 18550; nothing of a running backend is
touched), a data folder of its own for the manager's switch. Checks, with the switch off, on (each preset, a custom
text, an allowed address, an end time) and off again: the list for a new game and for one from before maintenance
mode, reserve, Quick Match, a server's registration, the manager's text checks, and the full server's message.

    python -I tools/test_maintenance.py
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

PY = sys.executable
HOST, FESL, THEATER, REG = '127.0.0.5', 18541, 18546, 18550
NEW = {'version': '0.4.7', 'platform': 'windows', 'notice': 1}   # a game with maintenance mode
OLD = {'version': '0.4.6.4', 'platform': 'windows'}              # a 0.4.6.4 game
FAILS = []


def check(what, ok, detail=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:400]))
    if not ok:
        FAILS.append(what)


def ask(obj, source=None):
    """One registry request (from `source`, a loopback address of its own, when given): the raw reply line."""
    s = socket.socket()
    s.settimeout(5)
    if source:
        s.bind((source, 0))
    s.connect((HOST, REG))
    s.sendall((json.dumps(obj) + '\n').encode())
    line = s.makefile().readline()
    s.close()
    return line


def quick_match(source=None):
    """Quick Match as the game asks it (FESL pnow/Start): the Status reply's resultType."""
    s = socket.socket()
    s.settimeout(5)
    if source:
        s.bind((source, 0))
    s.connect((HOST, FESL))
    s.sendall(plasma.build('pnow', plasma.REQUEST, 1, {'TXN': 'Start', 'partition.partition': '/xenon/BEACH',
                                                        'players.0.props.{sessionType}': 'findServer'}))
    got = []
    while len(got) < 2:
        head = b''
        while len(head) < 12:
            head += s.recv(12 - len(head))
        length = struct.unpack('>I', head[8:12])[0]
        body = b''
        while len(body) < length - 12:
            body += s.recv(length - 12 - len(body))
        fields = plasma.parse_payload(body)
        if head[:4] == b'pnow':
            got.append(fields)
    s.close()
    return got[1].get('props.{resultType}', '')


def manager(*words, data):
    env = dict(os.environ, BF_SERVER_DATA=data)
    p = subprocess.run([PY, '-I', os.path.join(ROOT, 'server', 'manager.py'), 'maintenance'] + list(words),
                       capture_output=True, text=True, env=env, timeout=60)
    return p.returncode, (p.stdout + p.stderr).strip()


def main():
    tmp = tempfile.mkdtemp(prefix='bf-maint-')
    data = os.path.join(tmp, 'data')
    os.makedirs(data)
    switch = os.path.join(data, 'maintenance.json')
    backend = subprocess.Popen([PY, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST, '--port', str(FESL),
                                '--theater-port', str(THEATER), '--registry-port', str(REG), '--login-delay', '0',
                                '--no-create', '--maintenance-file', switch, '--log', os.path.join(tmp, 'plasma.log')],
                               cwd=ROOT, stdout=open(os.path.join(tmp, 'plasma.out'), 'w'), stderr=subprocess.STDOUT)
    servers = []
    try:
        for _ in range(50):
            try:
                socket.create_connection((HOST, REG), timeout=1).close()
                break
            except OSError:
                time.sleep(0.2)

        def register(name, most, players):
            s = socket.create_connection((HOST, REG), timeout=5)
            s.sendall((json.dumps({'op': 'register', 'name': name, 'port': 18560 + len(servers), 'map': 'Wake Island',
                                   'max_players': most, 'tick': 60}) + '\n').encode())
            reply = json.loads(s.makefile().readline())
            s.sendall((json.dumps({'op': 'status', 'players': players, 'map': 'Wake Island'}) + '\n').encode())
            servers.append(s)
            return reply

        open_one = register('Open one', 24, 3)['gid']
        full_one = register('Full one', 2, 2)['gid']
        time.sleep(0.5)

        # ---- off
        listed = json.loads(ask(dict(NEW, op='list')))
        check('off: the list has both servers and no notice',
              len(listed['servers']) == 2 and 'notice_kind' not in listed, listed)
        check('off: reserve answers reserved', json.loads(ask(dict(NEW, op='reserve', gid=open_one)))['op'] == 'reserved')
        reply = json.loads(ask(dict(NEW, op='reserve', gid=full_one)))
        check('full server: op full with a title and a message',
              reply['op'] == 'full' and reply.get('title') == 'SERVER FULL' and '2 of 2 players' in reply.get('message', ''),
              reply)
        raw = ask(dict(OLD, op='reserve', gid=full_one))
        check('full server: nothing a 0.4.6.4 game reads as a yes', 'reserved' not in raw, raw)
        code, words = manager('status', data=data)
        check('manager status: off', code == 0 and words.startswith('maintenance: off'), words)

        # ---- the manager's checks
        for bad, why in (('x' * 201, 'too long'), ('Back soon "really"', 'a quote'), ('Slots reserved for admins', 'reserved'),
                         ('Café closed', 'a letter the fonts lack'), ('{"gid": 1}', 'braces')):
            code, words = manager('on', '--text', bad, data=data)
            check('manager refuses a custom text with %s' % why, code == 1 and not os.path.exists(switch), words)
        code, words = manager('on', '--preset', 'restart', '--text', 'hello', data=data)
        check('manager refuses a text with a preset', code == 1, words)
        code, words = manager('on', '--allow', 'not-an-address', data=data)
        check('manager refuses an allowed address that is not one', code == 1, words)

        # ---- each preset
        for kind, (title, text) in plasma.NOTICE_PRESETS.items():
            if kind == 'custom':
                continue
            check('preset %s passes the text check' % kind, not plasma.notice_text_problem(text))
            code, words = manager('on', '--preset', kind, data=data)
            time.sleep(1.2)
            new = json.loads(ask(dict(NEW, op='list')))
            check('%s: a new game gets the notice and the real servers' % kind,
                  new.get('notice_kind') == kind and new.get('notice_title') == title and new.get('notice_text') == text
                  and len(new['servers']) == 2, new)
            raw = ask(dict(NEW, op='reserve', gid=open_one))
            reply = json.loads(raw)
            check('%s: reserve answers closed with the text' % kind,
                  reply == {'op': 'closed', 'kind': kind, 'title': title, 'message': text} and 'reserved' not in raw, raw)
        code, words = manager('on', '--preset', 'deploy', data=data)
        check('manager on: says what the players see', code == 0 and 'Players see: MAINTENANCE: Unable to connect' in words,
              words)
        time.sleep(1.2)
        old_raw = ask(dict(OLD, op='list'))
        old = json.loads(old_raw)
        check('deploy: a 0.4.6.4 game gets one row with the text', len(old['servers']) == 1 and
              old['servers'][0]['description'] == plasma.NOTICE_PRESETS['deploy'][1] and
              old_raw.find('{"gid"') > old_raw.find('notice_text'), old)
        gate = json.loads(ask({'op': 'list'}))
        check('deploy: a game from before the version gate gets the row too', len(gate['servers']) == 1 and
              gate['servers'][0]['gid'] == 'notice-deploy', gate)
        raw = ask(dict(OLD, op='reserve', gid='notice-deploy'))
        check('deploy: the row picked in a 0.4.6.4 game: an answer at once, not a yes',
              json.loads(raw)['op'] == 'closed' and 'reserved' not in raw, raw)
        check('deploy: Quick Match finds no server', quick_match() == 'NOSERVER')
        reg = register('Registered during maintenance', 24, 0)
        time.sleep(0.3)
        check('deploy: a server still registers', reg.get('op') == 'registered', reg)
        check('deploy: and is on the new game\'s list', len(json.loads(ask(dict(NEW, op='list')))['servers']) == 3)
        check('deploy: Direct connect still finds a server',
              json.loads(ask(dict(NEW, op='find', address='127.0.0.1', port=18560)))['server'] is not None)  # (a server on this machine: --theater-host)

        # ---- an allowed address (an admin)
        code, words = manager('on', '--preset', 'deploy', '--allow', '127.0.0.9', data=data)
        time.sleep(1.2)
        admin = json.loads(ask(dict(NEW, op='list'), source='127.0.0.9'))
        check('allowed address: no notice', 'notice_kind' not in admin and len(admin['servers']) == 3, admin)
        check('allowed address: reserve answers reserved',
              json.loads(ask(dict(NEW, op='reserve', gid=open_one), source='127.0.0.9'))['op'] == 'reserved')
        check('allowed address: Quick Match joins its pick', quick_match(source='127.0.0.9') == 'JOIN')
        check('other address: still closed', json.loads(ask(dict(NEW, op='reserve', gid=open_one)))['op'] == 'closed')

        # ---- a custom text with an end time
        custom = 'New maps going in tonight. Join our Discord for the news & try again at 21:00 (UK time)!'
        code, words = manager('on', '--text', custom, '--minutes', '10', data=data)
        check('manager on with a custom text and minutes', code == 0 and '10 minutes left' in words, words)
        time.sleep(1.2)
        new = json.loads(ask(dict(NEW, op='list')))
        check('custom: the text and the time left', new.get('notice_kind') == 'custom' and
              new.get('notice_text') == custom + ' Expected back in about 10 minutes.' and
              540 < new.get('notice_seconds', 0) <= 600, new)
        with open(switch, encoding='utf-8') as f:
            raw = json.load(f)
        raw['until'] = int(time.time()) + 4
        with open(switch, 'w', encoding='utf-8') as f:
            json.dump(raw, f)
        time.sleep(1.2)
        check('end time: on before it', json.loads(ask(dict(NEW, op='reserve', gid=open_one)))['op'] == 'closed')
        time.sleep(3.5)
        reply = json.loads(ask(dict(NEW, op='list')))
        check('end time: off by itself after it', 'notice_kind' not in reply and
              json.loads(ask(dict(NEW, op='reserve', gid=open_one)))['op'] == 'reserved', reply)
        code, words = manager('status', data=data)
        check('manager status: its end time has passed', 'end time has passed' in words, words)

        # ---- off again
        manager('on', '--preset', 'emergency', data=data)
        time.sleep(1.2)
        code, words = manager('off', data=data)
        time.sleep(1.2)
        reply = json.loads(ask(dict(OLD, op='list')))
        check('off again: a 0.4.6.4 game has the servers', code == 0 and len(reply['servers']) == 3 and
              'notice_kind' not in reply, reply)
        check('off again: Quick Match', quick_match() == 'JOIN')
        log = open(os.path.join(tmp, 'plasma.log'), encoding='utf-8', errors='replace').read()
        check('the backend log says when it went on and off',
              'maintenance: on (deploy)' in log and 'maintenance: off' in log and 'refused game' in log)
    finally:
        for s in servers:
            s.close()
        backend.terminate()
        backend.wait(10)
    print('%d failed' % len(FAILS) if FAILS else 'all passed')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
