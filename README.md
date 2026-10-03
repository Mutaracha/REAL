![REAL](img/logo.png)

---

**REAL** keeps the Windows audio engine of the default playback device at the smallest
buffer size supported by its driver, which reduces playback latency for *every*
application that uses that device.

This is a fork of [miniant-git/REAL](https://github.com/miniant-git/REAL) (v0.2.0, 2019)
that is still maintained and adds quality-of-life features:

* proper window that can be closed/minimised to the system tray, with a menu bar
  (**REAL**: the mode, restart, exit; **Options**: Settings, Start with Windows;
  **Diagnostics**; **About**) and a small restart button at the end of the status line
* tray menu: the status, **REAL is running** (on/off), **Restart now** — the log,
  diagnostics — **Settings** — **Exit**
* restart of the audio streams without restarting the application — automatically
  on device changes, sleep/resume and session unlock, manually via the menus, the
  restart button of the window or a hotkey
* no forced updates: one check at startup (can be switched off), nothing is
  downloaded or installed, the application is never closed for it
* external settings file next to the executable (`real.settings.json`), edited
  either in the **Settings** window of the program or in a text editor
* Windows 11 tweaks (EcoQoS for the idle process of REAL, informative HRESULT diagnostics)

## Features

* Audio latency reduction on the default playback device (and optionally on the
  default capture device). Devices whose driver has nothing smaller than the
  default buffer (Bluetooth, HDMI, some vendor drivers) are reported instead of
  holding a stream that changes nothing
* Automatic re-application when the default device changes, when a device is
  switched on or off (plugged in or out), after resume from sleep or after the
  audio service restarts; the mode is switched back on when it was off
  (`reinit.enableWhenDisabled`)
* Minimises to the system tray; the tray icon survives an `explorer.exe` restart
* Global hotkeys, off by default (`Ctrl+Alt+L` — toggle, `Ctrl+Alt+R` — restart the
  audio streams)
* Optional autostart with Windows
* Update check at startup only (on by default, `updates.checkOnStartup`): one
  request to the page of the latest release on github.com, nothing is installed
  silently, no requests while running; the repository is fixed in the build
* Interface and log in English or Russian, picked from the Windows UI language
  on the first run (`application.language`: `auto`, `en`, `ru`)
* Settings in a plain JSON file that documents every option with comments
  (in the same language as the interface: a change of `application.language`
  rewrites the comments on the next start and keeps every value)
* One notification per device outage instead of one per retry; if the device
  stays silent for `audio.reinit.failureTimeoutMs` (60 s), the mode is switched
  off and the device is not polled anymore
* Always a single instance: a second start (`REAL.exe --reinit` and the like)
  passes its command to the running one and exits

## Requirements

* Windows 10 64-bit or Windows 11
* No Visual C++ Redistributable needed: the executable links the C runtime
  statically

## Setup

1. (Recommended) Install the in-box HDAudio driver, it usually supports smaller
   buffers than the vendor driver does:
    1. Start **Device Manager**.
    2. Under **Sound, video and game controllers**, double click the device that
       corresponds to your speakers.
    3. Open the **Driver** tab → **Update driver** →
       **Browse my computer for driver software** →
       **Let me pick from a list of available drivers on my computer**.
    4. Select **High Definition Audio Device** and click **Next**, then **Close**.
    5. Reboot if Windows asks for it.
    > **Be careful**: the new driver might reset your volume to uncomfortably high
    > levels. On Windows 11 the entry may be hidden until *Show compatible
    > hardware* is unchecked.
2. Download the latest release (or build it yourself, see below).
3. Launch `REAL.exe`. The latency reduction is active as long as the application
   is running — the window may be closed to the tray.

The status line of the window and the tray tooltip show the buffer size currently
used by the audio engine, for example `2.67 ms - Speakers (Realtek Audio)`.

## Command-Line Options

| Option | Description |
|---|---|
| *none* | Start with the main window (tray icon is created as well) |
| `--tray` | Start minimised to the system tray |
| `--no-tray` | Start with the main window visible |
| `--config <path>` | Use another settings file |
| `--no-config` | Ignore the settings file, use built-in defaults |
| `--log-level <level>` | `off`, `error`, `warn`, `info`, `debug`, `trace` |

Commands for a running instance (the command is passed to it and this process exits):

| Option | Description |
|---|---|
| `--reinit` | Restart: re-create the audio streams (scripts, shortcuts) |
| `--enable` / `--disable` | Enable / disable the latency reduction |
| `--exit` | Close the running instance |

Commands that run in this process:

| Option | Description |
|---|---|
| `--diagnose` | Write a report about the audio devices and drivers to `REAL-diagnostics.txt` |
| `--help`, `-h`, `/?` | Show help |
| `--version` | Show the version |

Options are case-insensitive. An unknown option is reported in the log and
ignored. `--tray`, `--no-tray` and `--log-level` hold for that run only:
they are never written to the settings file, and the autostart entry follows the
file.

## Configuration

`real.settings.json` is created next to `REAL.exe` on the first run. It is plain
JSON (`//` and `/* */` comments are allowed) and every option is explained by a
comment. **Options → Settings** in the menu bar (or **Settings** in the tray menu)
opens a window with the same parameters: it
writes that very file, so the file stays the source of truth, and the button
**Open the file** keeps the manual way available (together with `--config`),
**Reload** reads the file into the window again. Command-line options override the
file for the current run. A file that cannot be parsed is reported in the log and
by one notification, the defaults are used, and the text of the file is kept as
`real.settings.json.bad` before anything is written over it. A step-by-step guide in Russian is available in
[docs/usage.ru.md](docs/usage.ru.md). See [docs/CONFIG.md](docs/CONFIG.md) for the full reference and
[docs/real.settings.example.json](docs/real.settings.example.json) for an
annotated example.

The most important options:

```jsonc
{
  "application": { "minimizeToTray": true, "closeButtonAction": "exit" },
  "audio":       { "dataFlow": "render", "periodSelection": "min" },  // all default devices (default + communication)
  "updates":     { "checkOnStartup": true },  // one check at startup, false - no request at all
  "logging":     { "level": "off" }           // "info" (or another level) writes REAL.log
}
```

## Building

There is nothing to install besides a C++ compiler: the project has no external
dependencies (spdlog, nlohmann/json and tl::expected are header-only and vendored
in `real-app/deps/`).

### Option 1 — GitHub Actions (no local toolchain)

Push to your fork and download `REAL.exe` from the *Artifacts* section of the
build run, or push a `v*` tag to have it attached to a GitHub release.
The workflow is in [.github/workflows/build.yml](.github/workflows/build.yml).

### Option 2 — MSVC only (`build.bat`)

```bat
cd real-app
build.bat
```

It locates any installed Visual Studio / Build Tools (2017 or newer) through
`vswhere`, compiles the sources and resources and produces `real-app\build\REAL.exe`.

### Option 3 — CMake

```bat
cd real-app
run-cmake.bat          REM configures and builds
```

or manually:

```bat
cmake -S real-app -B build -A x64
cmake --build build --config Release
```

> With **Visual Studio 2026 (Build Tools 18)** the CMake generator
> `"Visual Studio 18 2026"` requires **CMake 4.2 or newer**. Older CMake
> versions report "could not find any instance of Visual Studio"; in that case
> use the Ninja generator (`cmake -S real-app -B build -G Ninja` after
> `vcvars64.bat`) or just `build.bat`.

The resulting executable has no DLL dependencies and does not need the Visual C++
Redistributable.

## FAQ

### How does this work?

As described in Microsoft's
[Low Latency Audio FAQ](https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio#span-idfaqspanspan-idfaqspanfaq),
by default all applications in Windows 10/11 use 10 ms buffers to render audio.
If one application requests smaller buffers, the audio engine switches to that
buffer size for every client of the same endpoint and mode. REAL uses
`IAudioClient3::InitializeSharedAudioStream` to request the smallest period the
driver supports.

### What are the downsides?

The buffer runs out faster and has to be refilled more often, which increases the
chance of audible cracks when the CPU is busy. Windows may also reserve a processor
for the audio engine while such a stream exists: other programs get it last, and
monitoring tools show it underused. This is the low latency mode of Windows
itself; the diagnostics report shows such a processor in its CPU line. The
priority of the REAL process does not change either effect: the sound is
processed by the Windows Audio service, not by REAL.

### How do I report a problem?

Choose **Diagnostics → Diagnostics** in the menu bar of the window (or run
`REAL.exe --diagnose`). It writes
`REAL-diagnostics.txt` next to the executable with your Windows version, the
active audio endpoints of the direction selected by `audio.dataFlow`, their
driver version and the periods they support, so the reason is visible without
guesswork. The blocks of the report are separated with `--- ... ---` lines, and
the periods of a device are listed one per line. The application log
(`REAL.log`) is off by default: pick a level in **Settings → Other → Log** when a
problem has to be traced.

Every version the program shows (the About window, `--version`, the diagnostics
report, the first line of the log) carries the build identification:
`v0.3.0 RC <run> (<commit>)`, where the number after `RC` is the number of the CI
run and the value in brackets is the commit the executable was built from. A build made from a
checkout shows the commit only.

### It says "the driver does not offer a period smaller than the default one"

The endpoint driver does not support small buffers. Typical cases: Bluetooth
audio (10 ms by design), HDMI/DisplayPort receivers, vendor drivers
(Realtek/Nahimic/ACX) and virtual devices. Installing the in-box
**High Definition Audio Device** driver usually fixes this for on-board codecs.

### The device changed and the effect disappeared

REAL re-applies the low latency mode automatically (default device changes, a
device switched on or off, resume from sleep, session unlock, audio service
restart, plus a check every 30 seconds). The limits are configurable in `audio.reinit`. If the
device does not answer, the retries back off and a single balloon is shown; after
`reinit.failureTimeoutMs` the mode is switched off and the device is not polled
until it appears again. To force it manually: tray menu → **Restart now**,
**REAL → Restart** in the menu bar, the round arrow at the end of the status line,
`Ctrl+Alt+R` (when the hotkeys are on) or `REAL.exe --reinit` — this also
switches the mode back on when it was off.

### Where are the logs?

The log file is off by default (`logging.level = "off"`). Any other level
writes `REAL.log` next to the executable (path and rotation in `logging`); the
window of the program always shows the operations at the `info` level, whatever
the settings say about the file. Use **Log file** in the tray menu, or
*Diagnostics → Open the log* in the window.
HRESULT failures are reported with their symbolic name, e.g.
`AUDCLNT_E_UNSUPPORTED_FORMAT (0x88890008)`.

## Licence

MIT, see [LICENCE](LICENCE). Original work © mini)(ant, 2018-2019; the initial
implementation of `IAudioClient3` handling is taken from miniant-git/REAL.
Background on the Windows 11 side effects was informed by
[spddl/LowAudioLatency](https://github.com/spddl/LowAudioLatency) (MIT).
