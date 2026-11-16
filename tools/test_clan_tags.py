"""Clan tags (TekRantGaming, 16 Nov 2026): checks of the rules and of the backend.

    python -I tools/test_clan_tags.py rules <text_filter_tests.exe>   the backend's rules (server/text_filter.py)
                                                        against the game's (bf/src/text_filter.h): the lists read out of
                                                        the header, and every answer of the header's test program
                                                        (bf/tests/text_filter_tests.cpp --tags) over a corpus
    python -I tools/test_clan_tags.py backend           the backend's tags (set, clear, change, refused with why, only
                                                        the name's own player, Dev only for the team names' signed
                                                        games, the rows and "tags") on a backend of its own (127.0.0.5,
                                                        ports 28341 to 28350, a test owner key)

The test program: clang++ -std=c++20 -I bf/src bf/tests/text_filter_tests.cpp -o text_filter_tests
"""
import asyncio
import itertools
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import ed25519  # noqa: E402
import text_filter  # noqa: E402

HOST = '127.0.0.5'


# ---- the rules
def header_lists():
    """Every string_view list of text_filter.h, by its C++ name."""
    text = open(os.path.join(ROOT, 'bf', 'src', 'text_filter.h'), encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'inline constexpr std::string_view (\w+)\[\] = \{(.*?)\};', text, re.S):
        out[m.group(1)] = tuple(re.findall(r'"([^"]*)"', m.group(2)))
    return out


def corpus():
    words = ['', 'A', 'AB', 'ABC', 'Wake', 'Iwo', 'UK', 'Fuzz', 'Boss', 'Zulu', '1943', 'x7', 'Tide', 'Fox', 'Raid',
             'Dev', 'DEV', 'dev', 'Devs', 'D3V', 'D3vs', 'xDvx', 'DDEV', 'Deev', 'Dvz', 'Devz', 'Dev1', 'Adm', 'Mod',
             'M0DS', 'EA', 'DICE', 'NOG', 'N0G', 'nogg', 'NIG', 'N1G', 'NlG', 'NGGA', 'FAG', 'FGT', 'KKK', 'FU', 'FUK',
             'Fck', 'STFU', 'SS', 'xSSx', '88', '1488', 'H88', 'ASS', 'A55', 'Cunt', 'Cum', 'Nazi', 'N4Z1', 'Kys',
             'A-B', 'A B', '[A]', 'ABCDE', 'Bull', 'Big', 'Shi', 'Zz', 'Xx', 'Nogo', 'Inga', 'Naga', 'Coco', 'Tit',
             'Titn', 'Sex', 'Sexy', 'Hoe', 'Ho3', 'Mass', 'Lass', 'Ahh', 'H8', '69', '420', 'G00K', 'Sp1c', 'Pak1']
    names = ['', 'Fox', 'shit', 'Ass', 'tHead', 'Hawk', 'Kestrel_7']
    rows = [(w, n) for w in words for n in names]
    rng = random.Random(1943)
    alphabet = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
    for _ in range(4000):
        rows.append((''.join(rng.choice(alphabet) for _ in range(rng.randint(1, 5))), rng.choice(names)))
    for a, b in itertools.product('dDeE3vVsSxz1lio0', repeat=2):
        rows.append(('d' + a + b, ''))
    return rows


def rules(exe):
    bad = []
    lists = header_lists()
    pairs = {'kAnywhere': 'ANYWHERE', 'kWords': 'WORDS', 'kNameOnly': 'NAME_ONLY', 'kInnocent': 'INNOCENT',
             'kTagReservedAnywhere': 'TAG_RESERVED_ANYWHERE', 'kTagReservedWhole': 'TAG_RESERVED_WHOLE',
             'kTagAnywhere': 'TAG_ANYWHERE', 'kTagWhole': 'TAG_WHOLE', 'kTagDigitsWhole': 'TAG_DIGITS_WHOLE',
             'kTagDigitsAnywhere': 'TAG_DIGITS_ANYWHERE'}
    for cpp, py in pairs.items():
        same = lists.get(cpp) == getattr(text_filter, py)
        print('%-62s %s' % ('list %s equals %s' % (cpp, py), 'ok' if same else 'FAILED'))
        if not same:
            bad.append(cpp)
    rows = corpus()
    text = ''.join('%s\t%s\n' % r for r in rows)
    out = subprocess.run([exe, '--tags'], input=text, capture_output=True, text=True, check=True).stdout
    got = [line.split('\t') for line in out.splitlines()]
    differ = []
    for (tag, name), line in zip(rows, got):
        want = line[2] if len(line) > 2 else ''
        py = text_filter.tag_problem(tag, name)
        if py != want:
            differ.append((tag, name, want, py))
    print('%-62s %s' % ('%d tags: the backend answers as the game' % len(rows),
                        'ok' if not differ and len(got) == len(rows) else 'FAILED %s' % differ[:10]))
    if differ or len(got) != len(rows):
        bad.append('answers')
    print('FAILED: %s' % bad if bad else 'ALL PASSED')
    return not bad


# ---- the backend
class Player:
    def __init__(self, port, code=None, pid=None, address=HOST):
        self.port, self.address = port, address
        self.seed = os.urandom(32) if code is None else code
        self.key = ed25519.public_key(self.seed).hex()
        self.pid = pid or '%08X' % random.randint(1, 0xFFFFFFFF)

    async def ask(self, m):
        r, w = await asyncio.open_connection(HOST, self.port, local_addr=(self.address, 0))
        w.write((json.dumps(m) + '\n').encode())
        await w.drain()
        line = await r.readline()
        w.close()
        return json.loads(line)

    async def signed(self, op, text):
        nonce = (await self.ask({'op': 'challenge'}))['nonce']
        body = '%s|%s|%s' % (op, nonce, text)
        return await self.ask({'op': op, 'key': self.key, 'signed': body,
                               'sig': ed25519.sign(self.seed, body.encode()).hex()})

    async def name(self, name, signed=False):
        if signed:
            return await self.signed('name', '%s|%s' % (name, self.pid))
        return await self.ask({'op': 'name', 'name': name, 'id': self.pid})

    async def tag(self, name, tag, pid=None):
        return await self.ask({'op': 'tag', 'name': name, 'id': pid or self.pid, 'tag': tag})

    async def tags(self, names, me=None):
        m = {'op': 'tags', 'names': names}
        if me:
            m['me'] = me
        return (await self.ask(m))['tags']


async def backend_checks(port, owner_seed):
    bad = []

    def check(what, ok, got=None):
        print('%-62s %s' % (what, 'ok' if ok else 'FAILED %s' % (got,)))
        if not ok:
            bad.append(what)

    a = Player(port)
    r = await a.name('Kestrel_7')
    check('the name check holds the name', r.get('name') == 'Kestrel_7', r)
    r = await a.tag('Kestrel_7', 'WAKE')
    check('a tag is set', r.get('ok') is True and r.get('tag') == 'WAKE', r)
    check('others see it', (await Player(port).tags(['Kestrel_7'])) == {'Kestrel_7': 'WAKE'})
    r = await a.tag('Kestrel_7', 'Iwo')
    check('a tag is changed', r.get('ok') is True and (await Player(port).tags(['Kestrel_7'])) == {'Kestrel_7': 'Iwo'},
          r)
    r = await a.tag('Kestrel_7', '')
    check('a tag is cleared', r.get('ok') is True and (await Player(port).tags(['Kestrel_7'])) == {}, r)
    await a.tag('Kestrel_7', 'Iwo')
    for tag, why in (('Dev', 'reserved'), ('D3V', 'reserved'), ('Devs', 'reserved'), ('xDvx', 'reserved'),
                     ('NOG', 'profanity'), ('N0G', 'profanity'), ('FU', 'profanity'), ('1488', 'profanity'),
                     ('A', 'length'), ('ABCDE', 'length'), ('A-B', 'chars'), ('[Dv]', 'chars')):
        r = await a.tag('Kestrel_7', tag)
        check('"%s" refused: %s, with its words' % (tag, why),
              r.get('ok') is False and r.get('why') == why and r.get('text') == text_filter.TAG_PROBLEM_TEXT[why] and
              r.get('tag') == 'Iwo', r)
    check('a refused tag leaves the one worn', (await Player(port).tags(['Kestrel_7'])) == {'Kestrel_7': 'Iwo'})
    b = Player(port)
    await b.name('Gull_9')
    r = await b.tag('Gull_9', 'Big')
    check('"Big" is fine for Gull_9', r.get('ok') is True, r)
    await b.name('Ass')  # a name the game refuses; the backend still checks a tag with it
    r = await b.tag('Ass', 'Big')
    check('a tag and its name read together: refused', r.get('why') in ('with_name', 'profanity'), r)
    thief = Player(port, address='127.0.0.6')
    r = await thief.tag('Kestrel_7', 'Haha')
    check("another player cannot set a name's tag", r.get('ok') is False and r.get('why') == 'not_yours', r)
    r = await thief.tag('Kestrel_7', 'Haha', pid=a.pid)
    check('nor with its player id', r.get('why') in ('not_yours', None) and
          (await Player(port).tags(['Kestrel_7'])) == {'Kestrel_7': 'Iwo'}, r)
    r = await Player(port).tag('Nobody_1', 'ABC')
    check('a name nobody holds cannot have one', r.get('why') == 'not_yours', r)
    # The developers' names: Dev, only for the owner's signed game.
    owner = Player(port, code=owner_seed)
    r = await owner.name('DevOwner', signed=True)
    check('the team owner signs in for the team name', r.get('name') == 'DevOwner' and r.get('protected'), r)
    r = await owner.tag('DevOwner', 'WAKE')
    check('the owner wears Dev whatever the game sends', r.get('ok') and r.get('tag') == 'Dev' and r.get('dev'), r)
    check('others see Dev', (await Player(port).tags(['DevOwner'])) == {'DevOwner': 'Dev'})
    imp = Player(port, address='127.0.0.7')
    r = await imp.name('DevOwner')
    check('an impostor asking for the team name is renamed', r.get('name') != 'DevOwner', r)
    r = await imp.tag(r.get('name', ''), 'Dev')
    check('and cannot wear Dev', r.get('ok') is False and r.get('why') == 'reserved', r)
    check('nor does the name it got', (await Player(port).tags([r.get('tag', '') or 'x'])) == {})
    # The rows and the "me" of a match's tag requests.
    c = Player(port)
    await c.name('Petrel_3')
    t = await c.tags(['Petrel_3', 'Kestrel_7', 'DevOwner'], me={'name': 'Petrel_3', 'id': c.pid, 'tag': 'GG'})
    check('"me" sets the asker\'s own tag with the others\'',
          t == {'Petrel_3': 'GG', 'Kestrel_7': 'Iwo', 'DevOwner': 'Dev'}, t)
    lb = await c.ask({'op': 'players'})
    check('the players rows answer', isinstance(lb.get('rows'), list), lb)
    return bad


def backend():
    plasma = os.path.join(ROOT, 'server', 'plasma.py')
    owner_seed = os.urandom(32)
    owner_key = ed25519.public_key(owner_seed).hex()
    with tempfile.TemporaryDirectory() as d:
        team = os.path.join(d, 'team.json')
        json.dump({'owners': [{'owner': 'DevOwner', 'key': owner_key, 'contains': ['DevOwner'], 'names': []}],
                   'left_free': []}, open(team, 'w', encoding='utf-8'))
        log = os.path.join(d, 'plasma.log')
        p = subprocess.Popen([sys.executable, '-I', plasma, '--host', HOST, '--port', '28341',
                              '--theater-port', '28346', '--registry-port', '28350',
                              '--accounts', os.path.join(d, 'accounts.json'), '--team-names', team,
                              '--leaderboard', os.path.join(d, 'leaderboard.json'), '--log', log,
                              '--accounts-save-every', '1', '--login-delay', '0'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        try:
            time.sleep(1.5)
            bad = asyncio.run(backend_checks(28350, owner_seed))
        finally:
            p.terminate()
            p.wait()
        lines = [x for x in open(log, encoding='utf-8', errors='replace') if 'clan tag' in x]
        print('%-62s %s' % ('the backend log notes the tags (%d lines)' % len(lines), 'ok' if lines else 'FAILED'))
        if not lines:
            bad.append('log')
        print('FAILED: %s' % bad if bad else 'ALL PASSED')
        return not bad


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'rules':
        sys.exit(0 if rules(sys.argv[2]) else 1)
    if what == 'backend':
        sys.exit(0 if backend() else 1)
    print(__doc__)
