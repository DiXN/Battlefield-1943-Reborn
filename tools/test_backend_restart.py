"""A server keeps its players when its link to the backend drops (server/gmhost.py serve, server/plasma.py
Registry.resume), without the game. 17 Nov 2026: every restart of the main backend dropped all the players of the US
Central server, which registers with it from across the ocean, and a link that timed out ended its host.

Everything on this PC's loopback: a backend of its own on 127.0.0.110 (FESL 18241, Theater 18246, registry 18250 on
that address only), a host (gmhost.py, no game behind it) listening on 127.0.0.111, and a stand in client that links to
the host with a link INIT and reads the host's tunnel id from the answer.
  1. The host registers; the client links; the backend is stopped and started again; the host registers again and
     keeps its ids: the same tunnel id answers the same client, and both logs say the ids were kept.
  2. The backend's counters moved past the kept ids: a second server registered now gets other ids.
  3. A backend from before (the released v0.4.6.7 package's plasma.py, BF_TEST_OLD_SERVER: its server folder) gives
     other ids: the host is replaced as before (a new tunnel id), with no error storm in its output.

    python -I tools/test_backend_restart.py [--keep]   (about a minute)
"""
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable
HOST = '127.0.0.110'
HOST_ADDR = ('127.0.0.111', 18900)
REG = 18250
FAILS = []


def check(what, ok, detail=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:500]), flush=True)
    if not ok:
        FAILS.append(what)


def read(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def wait_for(test, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if test():
                return True
        except (OSError, ValueError, KeyError, TypeError):
            pass
        time.sleep(0.3)
    return False


def ask(obj):
    s = socket.create_connection((HOST, REG), timeout=10)
    s.sendall((json.dumps(obj) + '\n').encode())
    line = s.makefile('rb').readline()
    return s, json.loads(line) if line else None


def tunnel_id(client, connident):
    """A link INIT from the stand in client; the host's tunnel id from its answer's control part (None: no answer)."""
    # What is queued (a host resends its first reliable packet to a client that never acknowledges) is not the answer.
    client.settimeout(0.3)
    try:
        while True:
            client.recvfrom(2048)
    except socket.timeout:
        pass
    client.settimeout(2)
    sub = struct.pack('>II', 1, connident)  # LINK: seq INIT, the client's connection id
    body = struct.pack('>H', (len(sub) << 4) | 1) + sub
    client.sendto(struct.pack('>H', len(body)) + body, HOST_ADDR)
    try:
        data, _ = client.recvfrom(2048)
    except socket.timeout:
        return None
    at, heads = 2, []
    while at + 2 <= len(data):
        h = struct.unpack_from('>H', data, at)[0]
        heads.append((h >> 4, h & 15))
        at += 2
        if at + sum(n for n, _ in heads) >= len(data):
            break
    for n, port in heads:
        if port == 7 and n >= 6:
            return struct.unpack_from('>I', data, at + 1)[0]
        at += n
    return None


def main():
    keep = '--keep' in sys.argv[1:]
    base = tempfile.mkdtemp(prefix='bfrestart_')
    procs = []

    def start(args, out):
        p = subprocess.Popen(args, stdout=open(out, 'a'), stderr=subprocess.STDOUT, cwd=base,
                             creationflags=0x08000000 if os.name == 'nt' else 0)
        procs.append(p)
        return p

    def backend(plasma, tag):
        return start([PY, '-I', plasma, '--host', HOST, '--theater-host', HOST,
                      '--log', os.path.join(base, 'backend_%s.log' % tag)], os.path.join(base, 'backend_%s.out' % tag))

    def stop(p):
        p.kill()
        p.wait(10)

    host_log, host_out = os.path.join(base, 'host.log'), os.path.join(base, 'host.out')
    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.bind(('127.0.0.112', 0))
    client.settimeout(2)
    try:
        b1 = backend(os.path.join(ROOT, 'server', 'plasma.py'), 'new1')
        check('the test backend answers', wait_for(lambda: ask({'op': 'list'})[1] is not None, 20))
        start([PY, '-I', os.path.join(ROOT, 'server', 'gmhost.py'), '--backend', '%s:%d' % (HOST, REG),
               '--listen', '%s:%d' % HOST_ADDR, '--game-server', '127.0.0.111:9', '--name', 'Restart test',
               '--log', host_log], host_out)
        check('the host registers', wait_for(lambda: 'registered with the backend' in read(host_log), 20),
              read(host_out)[-400:])
        first = tunnel_id(client, 0x11111111)
        check('the stand in client links and gets the host\'s tunnel id', first is not None)

        # 1. The backend restarts: the host keeps its ids and its client.
        stop(b1)
        check('the host sees its backend link end', wait_for(lambda: 'backend link lost' in read(host_log), 15),
              read(host_log)[-400:])
        b2 = backend(os.path.join(ROOT, 'server', 'plasma.py'), 'new2')
        check('the host registers again and keeps its ids',
              wait_for(lambda: 'kept this server\'s ids' in read(host_log), 30), read(host_log)[-600:])
        check('the restarted backend says the ids were kept',
              wait_for(lambda: 'kept the ids of its host' in read(os.path.join(base, 'backend_new2.log')), 10),
              read(os.path.join(base, 'backend_new2.log'))[-400:])
        again = tunnel_id(client, 0x11111111)
        check('the same client gets the same tunnel id (its link is kept)', again == first, (first, again))

        # 2. The counters moved past them: another server gets other ids.
        kept = re.search(r'kept the ids of its host \(user (\d+), player (\d+)\)',
                         read(os.path.join(base, 'backend_new2.log')))
        reg = {'op': 'register', 'name': 'Another', 'port': 18901, 'address': ['127.0.0.111', 18901], 'map': 'Wake Island'}
        s, reply = ask(reg)
        check('a second server gets ids after the kept ones', kept and reply and reply.get('op') == 'registered' and
              reply['uid'] > int(kept.group(1)) and reply['pid'] > int(kept.group(2)), (kept and kept.groups(), reply))
        s.close()

        # 2b. A link that died without the backend seeing it (the 16 Nov 08:59 time out): the old registration is
        # still open when the server registers again from the same address and port, so it is let go and the ids kept.
        # Another server (another port) asking for those ids gets others.
        stale = {'op': 'register', 'name': 'Stale', 'port': 18910, 'address': ['127.0.0.111', 18910], 'map': 'Wake Island'}
        s1, r1 = ask(stale)
        s2, r2 = ask(dict(stale, resume={'uid': r1['uid'], 'pid': r1['pid'], 'pids': []}))
        check('a server whose old registration is still open keeps its ids (from the same address and port)',
              r2 and r2.get('uid') == r1['uid'] and r2.get('pid') == r1['pid'], (r1, r2))
        s3, r3 = ask(dict(stale, port=18911, address=['127.0.0.111', 18911],
                          resume={'uid': r1['uid'], 'pid': r1['pid'], 'pids': []}))
        check('another server cannot take those ids', r3 and r3.get('uid') != r1['uid'], (r1, r3))
        for s_ in (s1, s2, s3):
            s_.close()

        # 3. A backend from before gives other ids: the host is replaced as before.
        old_dir = os.environ.get('BF_TEST_OLD_SERVER') or os.path.join(
            ROOT, '..', '..', '..', '..', 'BF-1942-MP-Decomp', 'runs', 'release', '0.4.6.7',
            'BF1943-Reborn-Server-0.4.6.7-windows', 'server')
        old_plasma = os.path.join(old_dir, 'plasma.py')
        if os.path.exists(old_plasma):
            stop(b2)
            # The old backend imports its own modules from its folder. A server registered there first, so the host
            # cannot get its old ids by chance (a fresh backend gives the first server the same ones).
            backend(old_plasma, 'old')
            check('the backend from before answers', wait_for(lambda: ask({'op': 'list'})[1] is not None, 20))
            first_there, _ = ask(dict(reg, name='First there'))
            check('with a backend from before, the host is replaced (other ids)',
                  wait_for(lambda: 'other ids' in read(host_log), 30), read(host_log)[-600:])
            replaced = tunnel_id(client, 0x22222222)
            check('the replaced host answers with a tunnel id of its own', replaced is not None and replaced != first,
                  (first, replaced))
        else:
            print('SKIP  a backend from before (set BF_TEST_OLD_SERVER to a v0.4.6.7 server folder)', flush=True)
        time.sleep(1)
        out = read(host_out)
        check('no tracebacks from the host', 'Traceback' not in out, out[-800:])
        print('host ids:', ' | '.join(l.split('GMHOST ')[1] for l in read(host_log).splitlines()
                                      if 'registered with' in l or 'ids' in l), flush=True)
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
        for p in procs:
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                pass
        client.close()
        if keep:
            print('folder kept: %s' % base)
        else:
            shutil.rmtree(base, ignore_errors=True)
    print('%d failed' % len(FAILS) if FAILS else 'all passed', flush=True)
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
