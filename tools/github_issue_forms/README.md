# Issue forms and labels for Report an issue

These go into the release repo, TekRantGaming/Battlefield-1943-Reborn, only on TekRantGaming's word (16 Nov 2026:
"keep everything local for now, I will let you know when we will put anything on git"). They are kept here, outside
any `.github` folder, so nothing on GitHub picks them up by accident.

- `ISSUE_TEMPLATE/*.yml` go to `.github/ISSUE_TEMPLATE/` on the release repo's default branch. The field ids (`what`,
  `steps`, `expected`, `actual`, `often`, `system`, `log`, `contact`) are the ones the game prefills on the browser
  route (bf/src/report.cpp BrowserUrl), and each form's file name is the one it asks for (`template=crash.yml`).
- `workflows/label-reports.yml` goes to `.github/workflows/`: it labels the reports the relay files as the bot
  account (which has no access to the repository, so GitHub drops the labels it asks for), by the title's [Type].
  Set the repository variable REPORT_BOT_LOGIN to the bot's login.
- `labels.yml` lists the labels the forms and the relay use. Made once, for example with the GitHub CLI:

```
gh label create crash --color b60205 --description "The game closed, froze or stopped answering" -R TekRantGaming/Battlefield-1943-Reborn
```

  (and the same for bug, performance, server, suggestion, from-game, needs-triage).

The design, the relay and the tests are in docs/REPORTS.md. tools/fake_report_relay.py stands in for the relay
locally and is the reference for the real one.
