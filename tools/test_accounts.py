"""Protect your name: checks of the account code (TekRantGaming, 15 Nov 2026).

    python -I tools/test_accounts.py crypto <kt.exe>   the game's SHA-512 and Ed25519 (bf/src/account_key.cpp, built
                                                        into a small test program) against hashlib, server/ed25519.py
                                                        and OpenSSL (openssl on the PATH, as Git for Windows has)
    python -I tools/test_accounts.py consts            the constants account_key.cpp holds, computed again
    python -I tools/test_accounts.py backend           the backend's accounts (claim, sign in, a name thief, recovery,
                                                        stats, renames, expiry, replays) on a backend of its own
                                                        (127.0.0.1, ports 28241 to 28250)

The test program: a main that answers "sha <hex>", "pub <seed hex>", "sign <seed hex> <message hex>" and
"code <code> <message>" with hex (scratchpad acct/kt.cpp of 15 Nov 2026).
"""
import asyncio
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import ed25519  # noqa: E402

ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ'


def seed_of(code):
    return hashlib.sha512(('BF1943 Reborn account\n' + code).encode()).digest()[:32]


def openssl_sign(seed, message):
    with tempfile.TemporaryDirectory() as d:
        der = bytes.fromhex('302e020100300506032b657004220420') + seed
        open(os.path.join(d, 'k.der'), 'wb').write(der)
        open(os.path.join(d, 'm'), 'wb').write(message)
        subprocess.run(['openssl', 'pkey', '-inform', 'DER', '-in', 'k.der', '-out', 'k.pem'], cwd=d, check=True,
                       capture_output=True)
        subprocess.run(['openssl', 'pkeyutl', '-sign', '-inkey', 'k.pem', '-rawin', '-in', 'm', '-out', 's'], cwd=d,
                       check=True, capture_output=True)
        return open(os.path.join(d, 's'), 'rb').read()


def crypto(exe):
    def run(*args):
        return subprocess.run([exe] + list(args), capture_output=True, text=True, check=True).stdout.split()
    bad = 0
    for n in list(range(0, 260)) + [1000, 4096]:
        m = os.urandom(n)
        if run('sha', m.hex())[0] != hashlib.sha512(m).hexdigest():
            print('SHA-512 differs at %d bytes' % n)
            bad += 1
    print('SHA-512: %d lengths checked' % 262)
    for i in range(40):
        seed, m = os.urandom(32), os.urandom(random.choice([0, 1, 31, 32, 64, 100, 200, 1000]))
        pub, sig = run('pub', seed.hex())[0], run('sign', seed.hex(), m.hex())[0]
        # (OpenSSL refuses to sign an empty file: an empty message is checked against ed25519.py alone.)
        want = openssl_sign(seed, m) if m else ed25519.sign(seed, m)
        if pub != ed25519.public_key(seed).hex():
            print('public key differs from ed25519.py', seed.hex())
            bad += 1
        if sig != want.hex():
            print('signature differs from OpenSSL', seed.hex(), m.hex())
            bad += 1
        if not ed25519.verify(bytes.fromhex(pub), m, bytes.fromhex(sig)):
            print('ed25519.py does not accept the game\'s signature')
            bad += 1
        if i == 0 and ed25519.verify(bytes.fromhex(pub), m + b'x', bytes.fromhex(sig)):
            print('ed25519.py accepted a changed message')
            bad += 1
    print('Ed25519: 40 keys and signatures checked against OpenSSL and ed25519.py')
    code = ''.join(random.choice(ALPHABET) for _ in range(20))
    typed = ' '.join(code[i:i + 5] for i in range(0, 20, 5)).lower().replace('0', 'o').replace('1', 'l')
    got = run('code', typed, 'hello')
    seed = seed_of(code)
    if got[0] != code or got[1] != ed25519.public_key(seed).hex() or got[2] != ed25519.sign(seed, b'hello').hex():
        print('the code\'s key differs', got, code)
        bad += 1
    print('recovery code: typed as "%s", read back and its key checked' % typed)
    print('FAILED: %d' % bad if bad else 'ALL PASSED')
    return bad == 0


def consts():
    def limbs(v):
        return ', '.join('0x%04x' % ((v >> (16 * i)) & 0xffff) for i in range(16))

    def primes(n):
        out, k = [], 2
        while len(out) < n:
            if all(k % p for p in out if p * p <= k):
                out.append(k)
            k += 1
        return out

    def iroot(x, n):
        lo, hi = 0, 1 << ((x.bit_length() // n) + 2)
        while lo < hi:
            m = (lo + hi + 1) // 2
            lo, hi = (m, hi) if m ** n <= x else (lo, m - 1)
        return lo
    src = open(os.path.join(ROOT, 'bf', 'src', 'account_key.cpp'), encoding='utf-8').read()
    want = {
        'kD2': limbs(2 * ed25519.D % ed25519.P), 'kX': limbs(ed25519._GX), 'kY': limbs(ed25519._GY),
        'kL': ', '.join('0x%02x' % b for b in ed25519.L.to_bytes(32, 'little')),
        'kH': ', '.join('0x%016xULL' % (iroot(p << 128, 2) & (2 ** 64 - 1)) for p in primes(8)),
        'kK': ', '.join('0x%016xULL' % (iroot(p << 192, 3) & (2 ** 64 - 1)) for p in primes(80)),
    }
    ok = all(v in src for v in want.values())
    print('constants: %s' % ('all as computed' if ok else 'DIFFERENT: ' + ', '.join(k for k, v in want.items()
                                                                                   if v not in src)))
    return ok


# ---- the backend
class Player:
    def __init__(self, port, code=None, pid=None, address='127.0.0.1'):
        self.port, self.address = port, address
        self.code = code or ''.join(random.choice(ALPHABET) for _ in range(20))
        self.seed = seed_of(self.code)
        self.key = ed25519.public_key(self.seed).hex()
        self.pid = pid or '%08X' % random.randint(1, 0xFFFFFFFF)

    async def ask(self, m):
        r, w = await asyncio.open_connection('127.0.0.1', self.port, local_addr=(self.address, 0))
        w.write((json.dumps(m) + '\n').encode())
        await w.drain()
        line = await r.readline()
        w.close()
        return json.loads(line)

    async def signed(self, op, text, nonce=None, **extra):
        if nonce is None:
            nonce = (await self.ask({'op': 'challenge'}))['nonce']
        body = '%s|%s|%s' % (op, nonce, text) if text else '%s|%s' % (op, nonce)
        m = {'op': op, 'key': self.key, 'signed': body, 'sig': ed25519.sign(self.seed, body.encode()).hex()}
        m.update(extra)
        return await self.ask(m), m

    async def name(self, name, signed=True):
        if not signed:
            return await self.ask({'op': 'name', 'name': name, 'id': self.pid})
        return (await self.signed('name', '%s|%s' % (name, self.pid)))[0]


async def fesl_login(port, gamertag, pid_hex, address):
    """The game's login as its packets go (server/plasma.py's framing): the persona the backend gives."""
    import plasma
    r, w = await asyncio.open_connection('127.0.0.1', port, local_addr=(address, 0))
    w.write(plasma.build('acct', plasma.REQUEST, 1, {'TXN': 'NuXBL360Login', 'gamertag': gamertag,
                                                       'xuid': str(int(pid_hex, 16))}))
    await w.drain()
    persona = None
    for _ in range(4):
        head = await asyncio.wait_for(r.readexactly(12), 10)
        body = await r.readexactly(int.from_bytes(head[8:12], 'big') - 12)
        if head[:4] == b'acct':
            persona = plasma.parse_payload(body).get('personaName')
            break
    w.close()
    return persona


async def backend_checks(port, accounts_file, proc_log):
    bad = []

    def check(what, ok, got=None):
        print('%-62s %s' % (what, 'ok' if ok else 'FAILED %s' % (got,)))
        if not ok:
            bad.append(what)
    a, thief = Player(port), Player(port, address='127.0.0.2')  # another player: another address
    r = await a.signed('claim', 'Kestrel_7|%s' % a.pid)
    check('a player protects a free name', r[0].get('ok') is True and r[0].get('name') == 'Kestrel_7', r[0])
    check('the same request again is refused (one time challenge)', (await a.ask(r[1])).get('ok') is not True)
    r = await thief.signed('claim', 'kestrel_7|%s' % thief.pid)
    check('another key cannot protect it (any case)', r[0].get('ok') is False and r[0].get('why') == 'taken', r[0])
    r = await thief.name('Kestrel_7', signed=False)
    check('a name check without the key gets a number', r.get('name') not in ('Kestrel_7', 'kestrel_7') and
          r.get('protected_by_other') is True, r)
    r = await Player(port, pid=a.pid, address='127.0.0.3').name('Kestrel_7', signed=False)
    check('even with the owner\'s player id copied', r.get('name') != 'Kestrel_7', r)
    r = await a.name('Kestrel_7')
    check('the owner signs in and gets the name', r.get('name') == 'Kestrel_7' and r.get('protected') is True, r)
    forged = {'op': 'name', 'key': a.key, 'signed': 'name|%s|Kestrel_7|%s' % ((await a.ask({'op': 'challenge'}))['nonce'],
                                                                              thief.pid),
              'sig': ed25519.sign(thief.seed, b'x').hex()}
    r = await a.ask(forged)
    check('a signature by another key is refused', r.get('name') != 'Kestrel_7' or r.get('protected') is not True, r)
    r = await a.ask({'op': 'name', 'key': a.key, 'signed': 'name|00000000000000000000000000000000|Kestrel_7|%s' % a.pid,
                     'sig': ed25519.sign(a.seed, ('name|00000000000000000000000000000000|Kestrel_7|%s' %
                                                  a.pid).encode()).hex()})
    check('a challenge the backend never gave is refused', r.get('protected') is not True, r)
    # stats: the owner's go to the account and the leaderboard; anyone else's for that name are refused
    stats = 'Kestrel_7|1200|3600|4|CQ_StringForFlagCapture=4,400;Kill=30,800|1,7'
    r = (await a.signed('stats_put', stats))[0]
    check('the owner\'s signed stats are kept', r.get('ok') is True, r)
    r = await thief.ask({'op': 'stats_put', 'name': 'Kestrel_7', 'score': 999999, 'seconds': 1, 'captures': 0})
    check('unsigned stats for a protected name are refused', r.get('ok') is False, r)
    r = (await thief.signed('stats_put', 'Kestrel_7|999999|1|0||'))[0]
    check('signed stats by another account for it are refused', r.get('ok') is False, r)
    lb = await a.ask({'op': 'leaderboard'})
    row = [x for x in lb.get('rows', []) if x['name'] == 'Kestrel_7']
    check('the leaderboard shows the owner\'s score', bool(row) and row[0]['score'] == 1200, lb)
    # recovery on a new PC: the code alone gives the name and the progress back
    b = Player(port, code=a.code)
    r = (await b.signed('recover', ''))[0]
    prog = r.get('progress') or {}
    check('the recovery code gives the name and progress back', r.get('ok') is True and r.get('name') == 'Kestrel_7'
          and prog.get('stats', {}).get('Kill') == '30,800' and prog.get('seconds') == 3600 and
          prog.get('achievements') == [1, 7], r)
    r = (await thief.signed('recover', ''))[0]
    check('a code with no account recovers nothing', r.get('ok') is False, r)
    r = await b.name('Kestrel_7')
    check('the new PC signs in with the name (another player id)', r.get('name') == 'Kestrel_7' and
          r.get('protected') is True, r)
    # a rename: the account moves to the new name and the old one is free
    r = (await a.signed('claim', 'Kestrel_8|%s' % a.pid))[0]
    check('the owner renames the protected name', r.get('ok') is True and r.get('name') == 'Kestrel_8', r)
    r = await thief.name('Kestrel_7', signed=False)
    check('the old name is free again', r.get('name') == 'Kestrel_7', r)
    r = await thief.name('Kestrel_8', signed=False)
    check('the new one is protected', r.get('name') != 'Kestrel_8', r)
    # the game's own login (FESL): the protected name only for the player id and address that signed in for it
    persona = await fesl_login(port - 9, 'Kestrel_8', a.pid, '127.0.0.1')
    check("the owner's game logs in under the protected name", persona == 'Kestrel_8', persona)
    persona = await fesl_login(port - 9, 'Kestrel_8', a.pid, '127.0.0.2')
    check('a game logging in under it from elsewhere is renamed', persona not in ('Kestrel_8', None), persona)
    # a name someone plays under right now cannot be taken from them by a claim
    online = Player(port)
    await online.name('Busy_One', signed=False)
    r = (await thief.signed('claim', 'Busy_One|%s' % thief.pid))[0]
    check('a name held by a player now cannot be claimed by another', r.get('ok') is False and
          r.get('why') == 'in_use', r)
    # bad requests
    r = (await thief.signed('claim', 'ab|%s' % thief.pid))[0]
    check('a name of 2 characters is refused', r.get('ok') is False, r)
    r = (await thief.signed('claim', 'Bad Name!|%s' % thief.pid))[0]
    check('a name with other characters is refused', r.get('ok') is False, r)
    r = await thief.ask({'op': 'claim', 'key': 'zz', 'signed': 'claim|x|y|z', 'sig': '00'})
    check('a malformed claim is refused', r.get('ok') is False, r)
    # the rate limit: 5 claims an hour from one address
    many = [Player(port) for _ in range(5)]
    oks = [(await p.signed('claim', 'Spam_%d|%s' % (i, p.pid)))[0].get('ok') for i, p in enumerate(many)]
    r = (await Player(port).signed('claim', 'Spam_9|%s' % thief.pid))[0]
    r2 = (await Player(port, address='127.0.0.4').signed('claim', 'Spam_10|%s' % thief.pid))[0]
    check('another address can still claim', r2.get('ok') is True, r2)
    check('at most 5 new names an hour from one address (with the 2 before)', r.get('why') == 'too_many', (oks, r))
    return bad


def backend():
    plasma = os.path.join(ROOT, 'server', 'plasma.py')
    with tempfile.TemporaryDirectory() as d:
        accounts = os.path.join(d, 'accounts.json')
        log = os.path.join(d, 'plasma.log')

        def start(extra=()):
            return subprocess.Popen([sys.executable, '-I', plasma, '--host', '127.0.0.1', '--port', '28241',
                                     '--theater-port', '28246', '--registry-port', '28250', '--accounts', accounts,
                                     '--leaderboard', os.path.join(d, 'leaderboard.json'), '--log', log,
                                     '--accounts-save-every', '1', '--login-delay', '0'] + list(extra),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        p = start()
        try:
            time.sleep(1.5)
            bad = asyncio.run(backend_checks(28250, accounts, log))
            time.sleep(2.5)  # the accounts file is written within a second of a change
        finally:
            p.terminate()
            p.wait()
        saved = json.load(open(accounts, encoding='utf-8'))
        names = sorted(x.get('name', '') for x in saved.get('accounts', []))
        print('%-62s %s' % ('the accounts file keeps them (%d)' % len(names),
                            'ok' if 'Kestrel_8' in names else 'FAILED %s' % names))
        if 'Kestrel_8' not in names:
            bad.append('file')
        # a restart reads the file: the name is still protected; and one unused 181 days is free again
        for x in saved['accounts']:
            if x.get('name') == 'Spam_0':
                x['seen'] = int(time.time()) - 181 * 86400
        json.dump(saved, open(accounts, 'w', encoding='utf-8'))
        p = start()
        try:
            time.sleep(1.5)

            async def after():
                t = Player(28250)
                r1 = await t.name('Kestrel_8', signed=False)
                r2 = await t.name('Spam_0', signed=False)
                return r1, r2
            r1, r2 = asyncio.run(after())
        finally:
            p.terminate()
            p.wait()
        for what, ok, got in (('after a restart the name is still protected', r1.get('name') != 'Kestrel_8', r1),
                              ('a name unused for 181 days is free again', r2.get('name') == 'Spam_0', r2)):
            print('%-62s %s' % (what, 'ok' if ok else 'FAILED %s' % (got,)))
            if not ok:
                bad.append(what)
        print('FAILED: %s' % bad if bad else 'ALL PASSED')
        return not bad


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'crypto':
        sys.exit(0 if crypto(sys.argv[2]) else 1)
    if what == 'consts':
        sys.exit(0 if consts() else 1)
    if what == 'backend':
        sys.exit(0 if backend() else 1)
    print(__doc__)
