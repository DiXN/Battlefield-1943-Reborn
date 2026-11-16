"""Totals for the website (TekRantGaming, 15 Nov 2026: the site shows lifetime players, players this month, total
hours played and total downloads). The main backend (plasma.py --stats FILE) keeps the first three here; the website
feed adds the downloads from GitHub. Only totals ever leave this module: no names, no addresses, no player ids.

What is counted, and where it comes from:
  - a player: one game install (its player id, the random number the game keeps in its settings and sends as the
    XUID's low word, bf/src/game_menu.cpp) that joined a match on a server listed with this backend: Theater's EGAM,
    the step every join takes. A player who only opens the menus is not counted. Ids are kept salted and hashed
    (SHA-256, 16 hex digits), never as they came.
  - players this month: the players who joined a match in the current calendar month (UTC).
  - hours played: the players each registered server reports (its "status", every 5 seconds) times the time until
    its next report, at most GAP seconds (a server that stops reporting stops counting), added up over every server.
    Player hosted matches (no registered server) send no status and add no hours.

The file is JSON, written whole or not at all (a temporary file, then a replace), by plasma.py every
--stats-save-every seconds when something changed, and when the backend stops:
    {"version": 1, "salt", "since", "seconds", "players": {hash: first seen}, "months": {"YYYY-MM": [hash, ...]},
     "imported": [notes on history added by tools/stats_from_logs.py]}
"""
import hashlib
import json
import os
import secrets
import time

GAP = 15.0          # seconds: the most one server status counts for (they come every 5)
MAX_PLAYERS = 2000000


def month_of(t):
    return time.strftime('%Y-%m', time.gmtime(t))


class Stats:
    def __init__(self, path=None):
        self.path = path or None
        self.salt = secrets.token_hex(16)
        self.since = int(time.time())
        self.seconds = 0.0
        self.players = {}   # hashed id -> first seen (UTC seconds)
        self.months = {}    # 'YYYY-MM' -> set of hashed ids
        self.imported = []
        self.live = {}      # a registered server's GID -> (its players at its last status, when)
        self.dirty = False

    # ---- the file
    def load(self):
        """Reads the file when there is one. A file that cannot be read is left alone (renamed .bad) and counting
        starts again, so the backend always starts."""
        if not self.path or not os.path.exists(self.path):
            return False
        try:
            with open(self.path, encoding='utf-8') as f:
                data = json.load(f)
            salt = str(data['salt'])
            players = {str(k): int(v) for k, v in data.get('players', {}).items()}
            months = {str(m): set(map(str, ids)) for m, ids in data.get('months', {}).items()}
            seconds = float(data.get('seconds', 0))
            since = int(data.get('since', time.time()))
            imported = list(data.get('imported', []))
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            try:
                os.replace(self.path, self.path + '.bad')
            except OSError:
                pass
            return False
        self.salt, self.players, self.months, self.seconds, self.since, self.imported = (
            salt, players, months, max(0.0, seconds), since, imported)
        return True

    def to_json(self):
        return {'version': 1, 'salt': self.salt, 'since': self.since, 'seconds': round(self.seconds, 1),
                'players': self.players, 'months': {m: sorted(ids) for m, ids in sorted(self.months.items())},
                'imported': self.imported}

    def save(self, force=False):
        if not self.path or not (self.dirty or force):
            return
        self.dirty = False
        try:
            with open(self.path + '.tmp', 'w', encoding='utf-8') as f:
                json.dump(self.to_json(), f, separators=(',', ':'))
            os.replace(self.path + '.tmp', self.path)
        except OSError:
            self.dirty = True

    # ---- counting
    def key(self, player_id):
        """A player id (a number, or the XUID as text: its low 32 bits) as the hash kept, or None."""
        try:
            pid = int(player_id) & 0xFFFFFFFF
        except (TypeError, ValueError):
            return None
        if not pid:
            return None
        return hashlib.sha256(('%s:%08X' % (self.salt, pid)).encode()).hexdigest()[:16]

    def joined(self, player_id, now=None):
        """A player joined a match. Returns True when it is a player never counted before."""
        h = self.key(player_id)
        if not h:
            return False
        now = now or time.time()
        new = h not in self.players
        if new:
            if len(self.players) >= MAX_PLAYERS:
                return False
            self.players[h] = int(now)
        month = self.months.setdefault(month_of(now), set())
        if h not in month:
            month.add(h)
            self.dirty = True
        self.dirty = self.dirty or new
        return new

    def add_time(self, gid, now):
        held = self.live.get(gid)
        if held:
            players, at = held
            if players > 0:
                self.seconds += players * min(GAP, max(0.0, now - at))
                self.dirty = True

    def status(self, gid, players, now=None):
        """A registered server's status: its player count now (the hours count from its last one)."""
        now = now or time.time()
        try:
            players = max(0, min(256, int(players)))
        except (TypeError, ValueError):
            players = 0
        self.add_time(gid, now)
        self.live[gid] = (players, now)

    def gone(self, gid, now=None):
        """A registered server disconnected."""
        self.add_time(gid, now or time.time())
        self.live.pop(gid, None)

    def totals(self, now=None):
        """The public totals, and nothing else."""
        now = now or time.time()
        month = month_of(now)
        return {'players': len(self.players), 'players_month': len(self.months.get(month, ())), 'month': month,
                'hours': int(self.seconds // 3600), 'since': time.strftime('%Y-%m-%d', time.gmtime(self.since))}
