"""The server manager's router port forwards (UPnP) against a fake router on this PC (16 Nov 2026: the forwards were
permanent and never removed, so a host who stopped hosting kept an open port on the router). The manager's router
search (Upnp.find) is replaced here, so no request ever reaches the real router of the PC that runs this.

    python -I tools/test_manager_upnp.py

Checks: a forward is asked for with a lease and recorded; a router that takes only permanent forwards (error 725 as
HTTP 500) gets one; a port no longer used, and UPnP turned off, take forwards away; the watch renews leased forwards;
the manager's run() takes every forward away when it closes; close() of nothing does nothing.
"""
import argparse
import http.server
import os
import re
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = tempfile.mkdtemp(prefix='bf-upnp-test-')
os.environ['BF_SERVER_DATA'] = DATA
os.environ['BF_SERVER_NO_HANDOVER'] = '1'
os.environ['BF_SERVER_UPDATE_FIRST'] = '86400'  # a source tree's "dev" version waits 10**9 s: too long for Windows
sys.path.insert(0, os.path.join(ROOT, 'server'))
import manager  # noqa: E402

FAILS = []
CALLS = []  # (action, port, proto, lease)
ROUTER = {'permanent_only': False}


def check(what, ok, detail=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:300]), flush=True)
    if not ok:
        FAILS.append(what)


class FakeRouter(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        action = re.search(r'<u:(\w+)', body).group(1)
        field = lambda name: (re.search(r'<%s>([^<]*)</%s>' % (name, name), body) or [None, ''])[1]
        lease = field('NewLeaseDuration')
        CALLS.append((action, field('NewExternalPort'), field('NewProtocol'), lease))
        if action == 'AddPortMapping' and ROUTER['permanent_only'] and lease not in ('', '0'):
            self.send_response(500)
            self.end_headers()
            self.wfile.write(b'<errorCode>725</errorCode>')
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>')


router = http.server.ThreadingHTTPServer(('127.0.0.1', 0), FakeRouter)
threading.Thread(target=router.serve_forever, daemon=True).start()
CONTROL = 'http://127.0.0.1:%d/ctl' % router.server_address[1]


def fake_find(self):
    self.control, self.service, self.local = CONTROL, 'urn:schemas-upnp-org:service:WANIPConnection:1', '192.0.2.10'
    return True


manager.Upnp.find = fake_find  # never the real router


def calls(action):
    return [c for c in CALLS if c[0] == action]


def wait_for(cond, seconds=5.0):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


# ---- Upnp itself
u = manager.Upnp()
check('close() with nothing mapped asks nothing', u.close() == [] and not CALLS)
failed = u.open([(18300, 'UDP')])
check('a forward is asked for with a lease', not failed and calls('AddPortMapping')[-1] == ('AddPortMapping', '18300', 'UDP', str(manager.Upnp.LEASE)), CALLS)
check('the lease is recorded', u.mapped == {(18300, 'UDP'): manager.Upnp.LEASE}, u.mapped)
ROUTER['permanent_only'] = True
failed = u.open([(18301, 'UDP')])
adds = calls('AddPortMapping')[-2:]
check('a router taking only permanent forwards gets one', not failed and adds[0][3] != '0' and adds[1][3] == '0', adds)
check('that one is recorded as permanent', u.mapped.get((18301, 'UDP')) == 0, u.mapped)
ROUTER['permanent_only'] = False
failed = u.close()
dels = sorted((c[1], c[2]) for c in calls('DeletePortMapping'))
check('close() takes every forward away', not failed and dels == [('18300', 'UDP'), ('18301', 'UDP')] and not u.mapped, (dels, u.mapped))

# ---- the manager: open, a port no longer used, UPnP off, renewal
CALLS.clear()
m = manager.Manager()
m.cfg['upnp'] = True
m.cfg['servers'] = [dict(m.cfg['servers'][0], port=18300)] if m.cfg['servers'] else [{'port': 18300}]
m.cfg['servers'].append(dict(m.cfg['servers'][0], port=18302))
m.own_backend = lambda: False
m.open_ports()
check('the manager forwards each server\'s port', wait_for(lambda: set(m.upnp.mapped) == {(18300, 'UDP'), (18302, 'UDP')}), m.upnp.mapped)
check('the page says the ports are open', wait_for(lambda: m.ports_note == 'Router ports opened (UPnP).'), m.ports_note)
m.cfg['servers'] = m.cfg['servers'][:1]
m.open_ports()
check('a server removed: its forward is taken away', wait_for(lambda: set(m.upnp.mapped) == {(18300, 'UDP')}) and
      ('DeletePortMapping', '18302', 'UDP', '') in CALLS, (m.upnp.mapped, CALLS))
before = len(calls('AddPortMapping'))
manager.Upnp.RENEW = 0.5
m.ports_at = 0
watcher = threading.Thread(target=m.watch, daemon=True)
watcher.start()
check('the watch asks for leased forwards again (renewal)', wait_for(lambda: len(calls('AddPortMapping')) > before, 6), CALLS[-3:])
m.cfg['upnp'] = False
m.open_ports()
check('UPnP turned off: every forward is taken away', wait_for(lambda: not m.upnp.mapped) and
      ('DeletePortMapping', '18300', 'UDP', '') in CALLS, (m.upnp.mapped, CALLS[-3:]))
m.quit.set()
watcher.join(5)

# ---- run(): the manager closing takes its forwards away
CALLS.clear()
manager.Upnp.RENEW = 1200
args = argparse.Namespace(port=0, no_browser=True, no_start=True)
args.port = 18299
runner = threading.Thread(target=manager.run, args=(args,), daemon=True)
runner.start()
check('run() started', wait_for(lambda: getattr(manager.Handler, 'manager', None) is not None and manager.Handler.manager is not m, 10))
rm = manager.Handler.manager
rm.cfg['upnp'] = True
rm.own_backend = lambda: False
rm.open_ports()
check('run(): the forward is made', wait_for(lambda: bool(rm.upnp.mapped)), rm.upnp.mapped)
rm.quit.set()  # as /api/quit does
runner.join(20)
check('run(): closing the manager takes its forwards away', not runner.is_alive() and not rm.upnp.mapped and
      calls('DeletePortMapping'), (runner.is_alive(), rm.upnp.mapped, CALLS))
check('the manager noted it', any('router ports closed (the manager closed)' in line for line in rm.lines), rm.lines[-4:])

router.shutdown()
print('%d failed' % len(FAILS))
sys.exit(1 if FAILS else 0)
