![REAL](img/logo.png)

**Documentation:** [User guide](docs/usage.en.md) · [Settings reference](docs/settings.en.md)<br>
**Документация:** [Инструкция](docs/usage.ru.md) · [Описание настроек](docs/settings.ru.md)

---

**REAL** (REduce Audio Latency) keeps the Windows audio engine at the smallest buffer the
driver of your audio device supports. Every program that plays through that device gets the
lower latency at once — games, music software, browsers, calls — typically 2–3 ms instead of
the default 10 ms.

This is a maintained fork of [miniant-git/REAL](https://github.com/miniant-git/REAL)
(v0.2.0, 2019).

## Download

Download `REAL.exe` from the [latest release](https://github.com/Mutaracha/REAL/releases/latest).
It is a single file: nothing to install, no Visual C++ Redistributable needed.

Requirements: Windows 10 or Windows 11, 64-bit.

## Quick start

1. Put `REAL.exe` into a folder of its own that you can write to, for example `C:\Tools\REAL`:
   the program keeps its settings file next to itself.
2. Start it. The file is not signed, so Windows SmartScreen may warn about it: click
   **More info**, then **Run anyway**.
3. The status bar of the window shows the buffer in use, for example
   `2.67 ms - Speakers (Realtek(R) Audio)`. The latency stays low as long as REAL is running;
   the window can be hidden in the tray.
4. To start REAL with Windows, turn on **Options → Start with Windows**.

Many on-board sound cards get much smaller buffers with the standard Windows driver: see
[the user guide](docs/usage.en.md#the-standard-windows-driver-recommended).

## Features

* A window with a menu bar, a journal of what the program does and a status bar; a tray icon
  with a menu; silent notifications while the window is hidden.
* Every default device of the chosen direction — playback, recording or both — the default
  communication device included.
* The smallest buffer of the driver or a fixed size within the range of the device.
* Automatic restart of the audio streams after a default device change, sleep and session
  unlock, plus a check every 30 seconds.
* A settings window, and the same settings in a commented `real.settings.json`.
* A diagnostics report about the audio devices, their drivers and the buffers they support.
* An update check once at startup, which can be switched off; nothing is downloaded or forced.
* English and Russian interface.

## How it works

By default Windows 10 and 11 mix sound in 10 ms buffers. When one program asks for a smaller
buffer, the audio engine uses it for every program on the same device (see Microsoft's
[low latency audio FAQ](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio#faq)).
REAL holds a stream open through `IAudioClient3::InitializeSharedAudioStream` with the smallest
buffer the driver supports. The downsides — a higher risk of crackles under a heavy load, a
processor core Windows may reserve for audio — are described in the
[user guide](docs/usage.en.md#1-what-real-does).

## Building

The project needs only a C++ compiler: spdlog, nlohmann/json and tl::expected are header-only
and vendored in `real-app/deps/`. The executable links the C runtime statically.

### GitHub Actions

Every push builds `REAL.exe` with [.github/workflows/build.yml](.github/workflows/build.yml),
runs a smoke test and publishes the file in the *Artifacts* of the run. A release gets the same
tested file: run the workflow with the version of the release (`v1.2.3`) or push a `v*` tag.
The version in `real-app/src/VersionNumber.h`, `real-app/CMakeLists.txt` and
`real-app/res/app.manifest` must match the tag.

### MSVC (`build.bat`)

```bat
cd real-app
build.bat
```

It finds an installed Visual Studio or Build Tools (2017 or newer) with `vswhere` and produces
`real-app\build\REAL.exe`.

### CMake

```bat
cd real-app
run-cmake.bat
```

or manually:

```bat
cmake -S real-app -B build -A x64
cmake --build build --config Release
```

> With **Visual Studio 2026 (Build Tools 18)** the generator `"Visual Studio 18 2026"` needs
> **CMake 4.2 or newer**. With an older CMake use the Ninja generator
> (`cmake -S real-app -B build -G Ninja` after `vcvars64.bat`) or `build.bat`.

## Reporting a problem

Choose **Diagnostics → Diagnostics** in the window, then open an
[issue](https://github.com/Mutaracha/REAL/issues) and attach `REAL-diagnostics.txt`. See
[the user guide](docs/usage.en.md#10-diagnostics-and-reporting-a-problem) for the log file.

## Licence

MIT, see [LICENCE](LICENCE). Original work © mini)(ant, 2018-2019; the initial implementation
of the `IAudioClient3` handling is taken from miniant-git/REAL. Background on the Windows 11
side effects was informed by [spddl/LowAudioLatency](https://github.com/spddl/LowAudioLatency)
(MIT).
