# Skyrun's QGroundControl

A fork of [QGroundControl](https://github.com/mavlink/qgroundcontrol) with one thing added: a
Copilot in the Plan view that turns a typed instruction into a mission. Everything else is
upstream and stays that way, so the fork rebases cleanly.

## Using it

Open **Plan**, press **Copilot** on the left tool strip, and say what you want:

> Take off to 40 m, fly a 200 m square around home, come back and land.

The mission draws on the map. Nothing reaches an aircraft until you press Upload, exactly as if
you had placed the items yourself. Each instruction replaces the whole plan.

The model gets three verbs — takeoff, waypoint, land — the same three buttons sitting above
Copilot in the tool strip. It answers with positions in metres from home, and QGroundControl
turns those into mission items for whichever airframe is selected. On a multirotor, land is a
return to launch, because that is what QGroundControl's own Land button does.

## Several aircraft at once

Name them and each gets its own mission:

> drone 1 cruise at 20 m to POI 1, drone 2 hover at POI 2, drone 3 orbit POI 3 at 60 m radius

The aircraft you are looking at keeps its mission on screen to look over. **Send** uploads to all
of them, that one included, so a fleet is two presses. Each aircraft plans around its own home, so
aircraft parked apart each fly from where they stand.

To fly them, use QGroundControl's own Fly view: **Select All**, then **Arm** and **Start**.

Nothing is uploaded to an aircraft that is already flying a mission, is a different kind of
aircraft to the plan, says it is not ready to fly, or reports an unhealthy sensor. It is named,
with the reason, and keeps its plan — fix the problem and press Send again. You can see the state
of the aircraft on screen; these are the ones you cannot.

## Before it works

The Copilot shells out to the `claude` command, so Claude Code has to be installed and signed in.
There is no API key to manage.

Home comes from the connected vehicle. With no vehicle, give it one to plan around:

```sh
QGroundControl --home -36.8485,174.7633
```

## Building on a Mac

Upstream's build works, with two local differences. There is no Homebrew on the Skyrun Macs, so
install Qt with QGC's own script instead of `just deps`; and GStreamer stays off, because its
macOS SDK installs through a `sudo` package and nothing here needs video.

```sh
python3 tools/setup/install_python.py dev
./tools/.venv/bin/python tools/setup/install_qt.py install --from-config \
    --host mac --target desktop --arch clang_64 --outdir ~/Qt
./tools/.venv/bin/python tools/configure.py --preset default \
    --qt-root ~/Qt/6.12.0/macos -- -DQGC_ENABLE_GST_VIDEOSTREAMING=OFF
./tools/.venv/bin/cmake --build --preset default --parallel
```

`install_python.py` needs uv 0.12.8 or newer; an older one fails with a version mismatch.

The Plan view tests cover the Copilot's wiring:

```sh
./tools/.venv/bin/ctest --preset default --build-config Debug \
    -R "PlanViewUITest|MissionControllerTest"
```

## Living beside a stock QGroundControl

The bundle identifier is `ai.skyrun.qgroundcontrol`, not upstream's, so macOS keeps the two
apart. Sharing one makes `open` launch whichever it feels like, which is a confusing way to
find out you have been testing the wrong binary.

## Tracking upstream

```sh
git remote add upstream https://github.com/mavlink/qgroundcontrol.git
git fetch upstream && git rebase upstream/master
```

## What the fork changes

| File | Why |
|---|---|
| `src/MissionManager/MissionCopilot.{h,cc}` | Asks the model, builds the items |
| `src/MissionManager/MissionController.{h,cc}` | Owns the copilot and exposes it to QML |
| `src/PlanView/MissionCopilotPanel.qml` | The box you type in |
| `src/PlanView/PlanView.qml` | The Copilot button and the panel |
| `src/Utilities/QGCCommandLineParser.{h,cc}`, `src/QGCApplication.{h,cc}` | `--home` |
