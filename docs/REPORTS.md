# Report an issue: the in game bug report form that files on GitHub

TekRantGaming, 16 Nov 2026: "it needs to have git hub integration for feedback with template to automatically submit
data give user text boxes n stuff you can expand upon this to better make this than i suggested".

Status: built in dev only. Nothing is on GitHub and nothing is deployed ("keep everything local for now, I will let
you know when we will put anything on git"). The game's default is the browser route with no relay address set; the
relay is tested against a local fake (tools/fake_report_relay.py) and a dry run.

Mockups: runs/ui-designs/build_report.py (out/r1_form.png, r2_review.png, r3_outcome.png).

## The pages

REPORT AN ISSUE on Home (and on the main menu once the game runs) opens three steps on one paper sheet:

1. **Your report.** Type of issue (Crash, Bug, Performance, Server or connection, Suggestion), Title (required, up to
   100), What happened (required, up to 2000), Steps to reproduce, What you expected, What happened instead (each
   optional, up to 1000), How often (Every time, Often, Sometimes, Once), and a Screenshot (optional). Suggestion asks
   "Your idea" and "Why it helps" instead of the steps and expected/actual.
   - Controller: up and down pick a field; left and right pick a type or how often; A opens the on screen keyboard
     at the right for a text field (QWERTY, numbers, punctuation, Shift, Space, New line, Delete, Done; LT shift, RT
     space, X delete, B done typing). RB goes on to step 2.
   - Keyboard: type straight in; Tab next field; Enter a new line in the long fields; Ctrl+Enter next step; Esc back.
   - Mouse: click a field, a chip, a key.
   - The draft is kept (user\reports\draft.json) if you leave the page or the game, until it is sent or cleared.
2. **What gets sent.** Every attached part with its own tick and its size, and the issue exactly as GitHub will show
   it (the same Markdown the relay or the browser gets). A ticks or unticks; RT and LT scroll the preview.
3. **Send.** Send now (the relay: no GitHub account), Open on GitHub with my account (the browser route), Copy the
   report (clipboard, for Discord). The result: sent (the issue's number, A opens it), already known (added to an
   open issue), or saved for later (kept, sent by itself at the next start; copy or GitHub instead).

## What is attached (each part can be unticked)

| Part | What | Ticked by default |
|---|---|---|
| Game | version, release or dev build, OS family, the game's tick rate | always |
| PC | Windows version and build, CPU name, cores and threads, RAM, GPU name and driver version | always |
| Display and graphics | resolution, window mode, refresh, renderer, the graphics settings (a fixed list of keys) | always |
| Last match | server name, official or community, map, players, when (never its address) | if one was played this run or the last |
| Crash report | the newest crash-*.txt or hang-*.txt from the last 7 days (text only; the minidump stays on the PC) | Crash type |
| Log | the last 400 lines (up to 40 KB) of bf1943.log, or of bf1943.previous.log when the last run crashed | not for Suggestion |
| Screenshot | the newest picture the game saved with F12 (user\screenshots), downscaled to 1280 wide JPEG | when there is one from the last day |
| Contact | the soldier name or a name the player types, if they want a reply | off (anonymous) |

The log of a run that crashed is kept: at each start bf1943.log is moved to bf1943.previous.log before the new one
opens (before this, the next start wrote over it).

### Scrubbed before anything is shown or sent (report.cpp Scrub; its cases in ScrubSelfTest)

Each one is replaced by a word in brackets, `[user]`, `[name]`, `[ip]`, `[recovery code]`, `[install]`, `[key]`,
`[id]`, `[email]` (brackets, not `<...>`, which GitHub would take for HTML). A line a rule cannot read is left out.

- The Windows user name (GetUserName and the user folder's name) and the PC name, anywhere in the text.
- Paths under the user folder: `C:\Users\<name>\...` becomes `C:\Users\[user]\...`; `/home/<name>/` the same.
- IPv4 and IPv6 addresses (127.0.0.1 kept, it tells us about local tests), and host:port pairs of them.
- The account recovery code (the settings' account_code, in any spacing or case) and its formatted form.
- The install id (install-id.txt), any 32+ character hex or base64 run (keys, tokens), Authorization and token=
  values, e-mail addresses.
- What the player types is scrubbed the same way (a pasted path or address).

## How it reaches GitHub

A GitHub token never ships in the game. Two routes were compared:

| | A. Browser, prefilled issue form | B. Relay on our side |
|---|---|---|
| Player needs | a GitHub account, and to sign in on the PC | nothing |
| Steam Deck, Big Picture, controller only | awkward: a browser opens over the game | one press in the game |
| Size | a URL: kept under 8000 bytes, so the form text and the system table fit, the log does not (copied to the clipboard to paste) | up to 60 KB body (GitHub's limit is 65,536 characters) |
| Screenshot | the folder opens with the file selected, the player drags it in | stored by the relay, linked in the issue |
| Labels | from the issue form template (applied for anyone) | set by the relay |
| Spam control | GitHub's own (an account per issue) | ours: per install rate limits, size and content checks, duplicates |
| Replies | GitHub e-mails the player | only through the contact they chose to give |
| Needs his word | the templates pushed to the public repo | the templates, a bot account or app token, and the Worker deploy |

**Recommendation: B, with A always offered beside it and used whenever B is off or fails.** Most players have no
GitHub account and many play with a controller; one press with no sign in is what gets reports filed, and the relay
is where rate limits and duplicate merging can live. A stays for players who want to follow their issue by e-mail.

### The relay (built and tested locally only; deploy on his word)

`POST <bf_report_endpoint>/v1/bf/reports` (the plan: a route on Kong's Worker beside /v1/bf/pipelines; the backend in
server/ would do too). Headers X-BF-Install and X-BF-Version; JSON body `{type, title, body, labels, signature,
version, screenshot_jpeg?}` (the body is the Markdown the review page shows; `<!-- screenshot -->` in it is where the
relay puts the screenshot's link; screenshot_jpeg is base64).

- The token lives only in the Worker's secrets: a bot account's token (his pick; see the rollout plan, step 2), the
  bot having no access to the repository itself.
- Rate limits per install id (X-BF-Install, the random UUID the pipelines use): 3 an hour, 10 a day; per IP 20 a day,
  counted in memory with a daily salt, never stored.
- Checks: JSON shape, title 8 to 100 characters, body up to 60 KB, screenshot up to 400 KB and a real JPEG, at most 5
  links, `@` mentions broken (`@\u200b`) so a report cannot ping anyone, no HTML but `<details>`/`<summary>`.
- Duplicates: the same install and the same title and text within a day is refused (409). Crashes carry a signature
  (exception, faulting module and offset, the first game function): the relay keeps signature to issue number, and
  a crash already filed and open gets a short comment (version, GPU, OS) instead of a new issue, capped at 50.
- Labels: from-game, needs-triage, and the type's (crash, bug, performance, server, suggestion).
- Answers: 201 `{issue, url}`, 200 `{duplicate_of, url}`, 409, 413, 429 (with Retry-After), 5xx. The game keeps the
  report on anything but 200 and 201.

### The game's side

- `bf_report_endpoint`: the live relay, https://kk-launcher-telemetry.tekrantgaming.workers.dev, by default since 16
  Nov 2026 (v0.4.7 testing). Empty means Send now is hidden and the browser route is the main button. Test runs
  (tools/run_game.ps1) pass a closed local port, so no test files a real issue; `BF_REPORT_ENDPOINT` overrides both.
- `BF_REPORT_DRY_RUN=1` (dev and player builds): nothing leaves the PC; the request (headers and JSON) and the
  browser URL are written to user\reports\dry-run-<time>.json and .url, and the page says DRY RUN.
- Offline or failed: user\reports\pending-<time>.json, tried again at the next start and every 10 minutes on the menu,
  at most 5 times; Home's row shows "1 WAITING". Copy is always offered.

## The repo side (tools/github_issue_forms, not pushed)

Kept outside any `.github` folder until he says, then copied into the release repo's `.github/ISSUE_TEMPLATE/`:

- `ISSUE_TEMPLATE/crash.yml`, `bug.yml`, `performance.yml`, `server.yml`, `suggestion.yml`: issue forms whose field ids
  match the game's browser route (`what`, `steps`, `expected`, `actual`, `often`, `system`, `log`, `contact`), each
  with its labels; `config.yml` (blank issues off, Discord as a contact link).
- `labels.yml`: the labels and colours (crash, bug, performance, server, suggestion, from-game, needs-triage).

## Testing (all local)

- Dry run: `BF_REPORT_DRY_RUN=1`. Send now writes user\reports\dry-run-<time>.md (the issue as GitHub would show it),
  .json (the request body) and .http (with its headers); GitHub instead writes dry-run-<time>.url (the link and what
  was copied for the Log box). Nothing is opened or sent.
- The fake relay: `python -I tools/fake_report_relay.py --out runs/<name>/relay` and the game with
  `BF_REPORT_ENDPOINT=http://127.0.0.1:18490`. It answers as the real one will and writes the issue it WOULD create
  (issue-<n>.md, the API request in issue-<n>.json). `--fail` tests the kept report.
- Dev switches: `BF_DEV_REPORT_FILL=1` types a made up crash report; `BF_DEV_REPORT_SELFTEST=1` logs the scrubbing's
  cases (report.cpp ScrubSelfTest).

## Rollout plan (16 Nov 2026, "Yes, plan it"; each step waits for his word where marked)

Order: the issue forms first (the browser route needs them and works with no server), then the relay, then a game
release that points at it. Nothing here is done yet.

### 1. Issue forms and labels on the release repo (DONE 16 Nov 2026 at his "can you do it all")

Pushed to the default branch in-dev (a9b622c, 40bc47c: the forms' names quoted, the Bug form's colon had hidden
it), not main; the seven labels made (the existing bug label's description changed to ours); the repository variable
REPORT_BOT_LOGIN = bobandersonplexv2-ui. Checked: the New issue page lists the five forms and Discord, and a game
link fills every field (looked at, not submitted).

- Copy tools/github_issue_forms/ISSUE_TEMPLATE/*.yml into the release repo's `.github/ISSUE_TEMPLATE/` (on its
  default branch, in-dev, which GitHub reads them from) and push.
- Make the seven labels (labels.yml) with `gh label create`.
- Check: GitHub's "New issue" page shows the five forms and Discord; a dev build's "Open on GitHub with my account"
  (not a dry run) opens the Crash form filled in. Looked at only, never submitted.
- Note: config.yml turns blank issues off for players; he can still open one with the URL's template left out.

### 2. Who files the issues: a bot account (his pick, 16 Nov 2026: "Bot account")

What he does, with his own logins (nothing of it goes through a chat or a file):
- Make a second GitHub account for the reports (GitHub allows one free machine account a person), its own e-mail
  address and two factor sign in. His: bobandersonplexv2-ui (16 Nov 2026), REPORT_BOT_LOGIN on the release repo.
- Give it no access to the repository: anyone can open issues on a public repository, and without access a leaked
  token can never push code. GitHub then drops the labels the relay asks for, so the release repo's
  label-reports workflow (tools/github_issue_forms/workflows) sets them from the title, for the bot's issues only
  (its login in the repository variable REPORT_BOT_LOGIN).
- Make a classic token on the bot account, scope `public_repo` only, a year's expiry (a fine grained token cannot
  act on a repository another account owns, as far as I know: to be checked when it is made). A reminder to renew
  it before it runs out; the game keeps reports while the relay answers 503 or 502, so a lapse loses nothing.
- Type it into the Worker: `npx wrangler secret put BF_REPORT_TOKEN`, and a random `BF_REPORT_SALT` the same way.

### 3. The relay route on the Worker (BUILT and tested locally, 16 Nov 2026; deployed by the King Kong coordinator after his OK in its chat)

- server/report_relay/bf_reports.js, to go in as server-api/src/bf_reports.js beside bf_pipelines.js (its own route
  and tables, nothing of King Kong's touched); the usage.js lines, the secrets and the tests are in
  server/report_relay/README.md. POST /v1/bf/reports with the checks (shape, sizes, links, @ mentions, HTML, rate
  limits per install and per salted address hash, the same report in a day, a known open crash commented instead of
  filed again), then the GitHub API call; GET /v1/bf/reports/shot/<id>.jpg serves a report's screenshot.
- D1 tables made by the module: bf_report_sent (install, salted address hash, content hash, time: pruned after 2
  days), bf_report_signatures (crash signature to issue, 90 days), bf_report_shots (the JPEG, 90 days).
- Tested: 9 tests with a fake GitHub (`npm test` in server/report_relay), and the dev game against the real code run
  locally (local_relay.mjs): filed as #3001 at the fake GitHub, its screenshot link served. No real issue made.
- First live check after the deploy: one report filed, looked at, then closed and deleted (his OK for that one issue).

### 4. The game (the next release)

- bf_report_endpoint's default set to the Worker's address in the release build; until the relay is live, releases
  ship with it empty (GitHub in the browser only).
- Before release: the Linux build (report.cpp's Linux paths are not built yet: screenshots are Windows only there,
  sending goes through curl), and the usual release tests with the form walked through.
- The privacy page (website) gets a line: what a report sends, that it is shown first, kept 90 days on our side.
- Release notes: "Report an issue now files bug reports on GitHub from the game".
