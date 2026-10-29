# Changelog

- The Copilot plans for several aircraft from one instruction. Name them — "drone 1 cruise at
  20 m to the north end, drone 2 hover over the shed, drone 3 orbit the mast" — and each gets its
  own mission. The aircraft you are looking at keeps its mission on screen; Send uploads the rest.
- Each aircraft plans around its own home, so drones parked apart each fly from where they stand.
- Loiter joins takeoff, waypoint and land. Orbiting a point is a loiter with a radius, hovering
  over one is a loiter without.
- A mission is held back from an aircraft that is already flying one, is a different kind of
  aircraft to the plan, says it is not ready to fly, or reports an unhealthy sensor. It keeps its
  plan, so fixing the problem and pressing Send again is enough.

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
