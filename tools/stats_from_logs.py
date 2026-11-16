"""The website's totals from the logs kept so far (server/stats.py), for a stats file to start from: the players who
joined a match and the hours played since the first official servers (8 Nov 2026), before plasma.py --stats counted
them itself.

    python -I tools/stats_from_logs.py --out stats.json EXTRACT[=UTC_OFFSET_HOURS] ...

EXTRACT is the output of tools/stats_history_extract.ps1 (run on the official servers' machine: its backend logs and
its servers' host logs, only the lines needed) or the same from a Linux server (grep of its host.log, see that
script). Each file in it starts with "#FILE<tab>path<tab>created<tab>last written", in that machine's local time;
UTC_OFFSET_HOURS is that machine's clock (the Hetzner machine: -8, Pacific; the US Central VPS: 1, CET).

What it reads:
  - players: a backend log's EGAM (a join), traced back to its player id: Theater's USER names the login key, the key
    comes from FESL's NuXBL360Login answer, whose request carries the XUID. A backend restart ("FESL on") starts the
    keys again. Kept as stats.py keeps them: salted hashes, by month.
  - hours: a host log's "joined: game packets now go to the server" to the same address's "left the game" or
    "silent for 30 s, dropped". A session still open when its host started again ("listening on") or its log ended
    ends there. Each session counts for at most MAX_SESSION hours (a session whose end was never written must not
    count for days).
Times are "HH:MM:SS" with no date: a file starts on its creation day and a time more than an hour before the one
above it is the next day.

Prints what it found. Nothing in the output names anyone: counts, and hashed ids for the players.
"""
import argparse
import datetime
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'server'))
import stats  # noqa: E402

MAX_SESSION = 6 * 3600
LINE = re.compile(r'^(\d\d):(\d\d):(\d\d) (.*)$')
HOST = re.compile(r'^GMHOST (\d+\.\d+\.\d+\.\d+:\d+) (joined: game packets|left the game|silent for \d+ s, dropped)')
XUID = re.compile(r"'xuid': '(\d+)'")
LKEY = re.compile(r"'lkey': '(BF1943LKEY\d+)'")
USER_LKEY = re.compile(r"'LKEY': '([^']*)'")


class Clock:
    """A log's "HH:MM:SS" times as UTC seconds: from the file's creation day, a day on at each step back."""
    def __init__(self, created, offset_hours):
        self.day = datetime.datetime.strptime(created[:10], '%Y-%m-%d')
        self.offset = datetime.timedelta(hours=offset_hours)
        self.prev = None

    def at(self, h, m, s):
        tod = int(h) * 3600 + int(m) * 60 + int(s)
        if self.prev is not None and tod < self.prev - 3600:
            self.day += datetime.timedelta(days=1)
        self.prev = tod
        local = self.day + datetime.timedelta(seconds=tod)
        return (local - self.offset).replace(tzinfo=datetime.timezone.utc).timestamp()


def files_in(path):
    """(path, created, last written, lines) for each file in an extract."""
    out, cur = [], None
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            if line.startswith('#FILE\t'):
                parts = line.split('\t')
                cur = (parts[1], parts[2], parts[3] if len(parts) > 3 else parts[2], [])
                out.append(cur)
            elif line.startswith('#'):
                cur = None
            elif cur is not None:
                cur[3].append(line)
    return out


def backend(lines, clock, st, report):
    fesl, keys, theater = {}, {}, {}
    joins = traced = 0
    for line in lines:
        m = LINE.match(line)
        if not m:
            continue
        t = clock.at(*m.groups()[:3])
        rest = m.group(4)
        if ' FESL on ' in ' ' + rest:
            fesl, keys, theater = {}, {}, {}
            continue
        parts = rest.split(' ', 2)
        if len(parts) < 3:
            continue
        peer, arrow, body = parts[0], parts[1], parts[2]
        if 'NuXBL360Login' in body and arrow == '->' and body.startswith('acct'):
            x = XUID.search(body)
            if x:
                fesl[peer] = x.group(1)
        elif 'NuXBL360Login' in body and arrow == '<-' and body.startswith('acct'):
            k = LKEY.search(body)
            if k and peer in fesl:
                keys[k.group(1)] = fesl[peer]
        elif arrow == '->' and body.startswith('USER '):
            k = USER_LKEY.search(body)
            if k:
                theater[peer] = keys.get(k.group(1))
        elif arrow == '->' and body.startswith('EGAM '):
            joins += 1
            xuid = theater.get(peer)
            if xuid:
                traced += 1
                st.joined(xuid, t)
                report['first'] = min(report.get('first', t), t)
                report['last'] = max(report.get('last', t), t)
    report['joins'] = report.get('joins', 0) + joins
    report['traced'] = report.get('traced', 0) + traced


def host(lines, clock, end, report):
    """Player seconds of one host log."""
    open_at, total, sessions, capped = {}, 0.0, 0, 0
    last = None

    def close(peer, t):
        nonlocal total, sessions, capped
        start = open_at.pop(peer)
        length = max(0.0, t - start)
        if length > MAX_SESSION:
            length, capped = MAX_SESSION, capped + 1
        total += length
        sessions += 1

    for line in lines:
        m = LINE.match(line)
        if not m:
            continue
        t = clock.at(*m.groups()[:3])
        rest = m.group(4)
        if rest.startswith('GMHOST listening on'):
            for peer in list(open_at):
                close(peer, t)
            last = t
            continue
        h = HOST.match(rest)
        if not h:
            continue
        last = t
        peer, what = h.groups()
        if what.startswith('joined'):
            if peer in open_at:
                close(peer, t)
            open_at[peer] = t
        elif peer in open_at:
            close(peer, t)
    for peer in list(open_at):
        close(peer, max(end, last or end))
    report['sessions'] = report.get('sessions', 0) + sessions
    report['capped'] = report.get('capped', 0) + capped
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', required=True, help='the stats file to write (must not exist yet)')
    ap.add_argument('extracts', nargs='+', help='EXTRACT=UTC_OFFSET_HOURS (offset default 0)')
    args = ap.parse_args()
    if os.path.exists(args.out):
        ap.error('%s exists: this makes a new stats file, it never adds to one' % args.out)
    st = stats.Stats(args.out)
    report, seconds = {}, 0.0
    for spec in args.extracts:
        path, _, off = spec.partition('=')
        offset = float(off) if off else 0.0
        for name, created, written, lines in files_in(path):
            if re.search(r'[\\/]crash-[^\\/]*[\\/]', name):   # a crash folder's copy of a log read already
                continue
            clock = Clock(created, offset)
            end = Clock(written, offset).at(written[11:13] or 0, written[14:16] or 0, written[17:19] or 0) \
                if len(written) >= 19 else 0
            base = os.path.basename(name.replace('\\', '/'))
            if base == 'backend.log':
                backend(lines, clock, st, report)
            elif base == 'host.log':
                seconds += host(lines, clock, end, report)
    st.seconds = seconds
    if 'first' in report:
        st.since = int(report['first'])
    st.imported.append({'from': 'tools/stats_from_logs.py', 'made': int(datetime.datetime.now().timestamp()),
                        'joins': report.get('joins', 0), 'joins_traced': report.get('traced', 0),
                        'sessions': report.get('sessions', 0), 'sessions_capped': report.get('capped', 0)})
    st.save(force=True)
    t = st.totals(report.get('last'))
    print('joins (EGAM): %d, traced to a player id: %d' % (report.get('joins', 0), report.get('traced', 0)))
    print('players: %d; by month: %s' % (len(st.players), ', '.join('%s %d' % (m, len(ids))
                                                                       for m, ids in sorted(st.months.items()))))
    print('sessions on the servers: %d (%d capped at %d hours)' % (report.get('sessions', 0), report.get('capped', 0),
                                                                   MAX_SESSION // 3600))
    print('hours played: %d' % t['hours'])
    print('since: %s' % t['since'])
    print('written: %s' % args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
