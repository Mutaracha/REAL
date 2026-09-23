![REAL](img/logo.png)

---

**REAL** keeps the Windows audio engine of the default playback device at the smallest
buffer size supported by its driver, which reduces playback latency for *every*
application that uses that device.

This is a fork of [miniant-git/REAL](https://github.com/miniant-git/REAL) (v0.2.0, 2019)
that is still maintained and adds quality-of-life features:

* proper window that can be closed/minimised to the system tray
* tray menu: enable/disable, re-initialise, settings, log, updates, autostart, exit
* re-initialisation without restarting the application — automatically on device
  changes, sleep/resume and session unlock, manually via the tray menu or a hotkey
* no forced update checks: checking is optional and never closes the application
* external settings file next to the executable (`real.settings.json`)
* Windows 11 tweaks (power throttling, informative HRESULT diagnostics)

## Features

* Audio latency reduction on the default playback device (and optionally on the
  default capture device)
* Automatic re-application when the default device changes, when a device is
  added/removed, after resume from sleep or after the audio service restarts
* Minimises to the system tray; the tray icon survives an `explorer.exe` restart
* Global hotkeys (default `Ctrl+Alt+L` — toggle, `Ctrl+Alt+R` — re-initialise)
* Optional autostart with Windows
* Optional manual update check (never installed silently, never blocks startup)
* Settings in a plain JSON file with comments allowed
* Single instance: `REAL.exe --reinit` talks to the running instance

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
| `--console` | Also attach a console window for the log output |
| `--config <path>` | Use another settings file |
| `--no-config` | Ignore the settings file, use built-in defaults |
| `--log-level <level>` | `trace`, `debug`, `info`, `warn`, `error`, `off` |
| `--multi-instance` | Do not reuse an already running instance |

Commands for a running instance (forwarded to it, this process exits):

| Option | Description |
|---|---|
| `--reinit` | Re-initialise the audio streams (useful in scripts/shortcuts) |
| `--enable` / `--disable` | Enable / disable the latency reduction |
| `--check-updates` | Check for a newer release |
| `--exit` | Close the running instance |
| `--help`, `-h`, `/?` | Show help |
| `--version` | Show the version |

## Configuration

`real.settings.json` is created next to `REAL.exe` on the first run. It is plain
JSON (`//` and `/* */` comments are allowed) and is re-read when you use
**Settings file…** in the tray menu or `--config`. Command-line options override
the file. See [docs/CONFIG.md](docs/CONFIG.md) for the full reference and
[docs/real.settings.example.json](docs/real.settings.example.json) for an
annotated example.

The most important options:

```jsonc
{
  "application": { "minimizeToTray": true, "closeButtonAction": "minimize" },
  "audio":       { "dataFlow": "render", "role": "console", "allowPeriodSnap": true },
  "updates":     { "mode": "off" },          // off | manual
  "logging":     { "toFile": true, "level": "info" }
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
chance of audible cracks when the CPU is busy. Windows also keeps CPU resources
ready for the audio subsystem while such a stream exists (some monitoring tools
show one busy core). REAL mitigates the second effect by lowering its own priority
and disabling power throttling (`performance.disablePowerThrottling`).

### It says "the driver does not offer a period smaller than the default one"

The endpoint driver does not support small buffers. Typical cases: Bluetooth
audio (10 ms by design), HDMI/DisplayPort receivers, vendor drivers
(Realtek/Nahimic/ACX) and virtual devices. Installing the in-box
**High Definition Audio Device** driver usually fixes this for on-board codecs.

### The device changed and the effect disappeared

REAL re-applies the low latency mode automatically (default device changes,
device add/remove, resume from sleep, session unlock, audio service restart, plus
a check every 30 seconds). The limits are configurable in `audio.reinit`. To force
it manually: tray menu → **Reinitialize now**, or press `Ctrl+Alt+R`, or run
`REAL.exe --reinit`.

### Where are the logs?

`REAL.log` next to the executable (configurable in `logging`). Use
**Open log** in the tray menu to open it, or the *Open log* button in the window.
HRESULT failures are reported with their symbolic name, e.g.
`AUDCLNT_E_UNSUPPORTED_FORMAT (0x88890008)`.

## Licence

MIT, see [LICENCE](LICENCE). Original work © mini)(ant, 2018-2019; the initial
implementation of `IAudioClient3` handling is taken from miniant-git/REAL.
Background on the Windows 11 side effects was informed by
[spddl/LowAudioLatency](https://github.com/spddl/LowAudioLatency) (MIT).
