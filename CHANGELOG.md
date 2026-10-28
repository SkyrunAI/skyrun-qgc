# Changelog

## v5.2.0-skyrun.1 — 2026-10-29

- The Plan view has a Copilot. Press it on the tool strip, type what you want — "take off to
  40 m, fly a 200 m square around home, come back and land" — and the mission draws on the map.
  It shells out to the `claude` command, so Claude Code has to be installed and signed in, and
  there is no API key to keep anywhere. Nothing reaches an aircraft until you press Upload.
- Starting QGroundControl with `--home <lat>,<lon>` plans missions around that point, for when
  no vehicle is connected to take a home position from.

---

QGroundControl's own user-facing release notes live in the user guide:

**[What's New →](docs/en/qgc-user-guide/getting_started/whats_new.md)**
