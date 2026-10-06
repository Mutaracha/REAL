# REAL — user guide

[Русская версия](usage.ru.md) · [Settings reference](settings.en.md)

REAL (REduce Audio Latency) lowers the audio latency of Windows 10 and 11 for every program
at once: games, music software, browsers, calls. This guide explains what the program does,
how to install it and how to use it.

## Contents

1. [What REAL does](#1-what-real-does)
2. [Installation](#2-installation)
3. [The window](#3-the-window)
4. [The tray icon](#4-the-tray-icon)
5. [Settings](#5-settings)
6. [Automatic restart](#6-automatic-restart)
7. [Notifications](#7-notifications)
8. [Updates](#8-updates)
9. [Command line](#9-command-line)
10. [Diagnostics and reporting a problem](#10-diagnostics-and-reporting-a-problem)
11. [Limitations](#11-limitations)
12. [Uninstalling](#12-uninstalling)

## 1. What REAL does

By default the Windows audio engine mixes sound in 10 ms portions (buffers). When a program
asks for a smaller buffer, the engine uses it for every program that plays through the same
device. REAL keeps such a stream open with the smallest buffer the driver of the device
supports, so the latency drops for all programs at once. A typical result is a buffer of
2–3 ms instead of 10 ms.

* REAL does not process sound and changes no system settings: it only keeps the stream open.
  Once REAL exits, Windows goes back to its default buffer.
* The default devices are handled. In Windows each direction has two of them — the default
  device and the default communication device (**Settings → System → Sound**); REAL handles
  both, and one device that has both roles is handled once.
* Playback devices are handled by default; recording devices can be added in the settings.
* Instead of the smallest buffer you can choose a fixed size (see [Settings](#5-settings)).

Side effects:

* A smaller buffer has to be refilled more often. Under a heavy load on the processor
  crackles become more likely. If you hear them, choose a fixed buffer larger than the
  minimum one.
* While such a stream exists, Windows may reserve a processor core for audio: other programs
  get it last, and monitoring tools show it underused. This is the low latency mode of
  Windows itself; the **CPU** line of the diagnostics report shows such a core.
* The processor wakes up more often, so a laptop runs a little shorter on battery.
* The priority of the REAL process changes none of this: the sound is processed by the
  Windows Audio service, not by REAL.

## 2. Installation

1. Download `REAL.exe` from the page of the
   [latest release](https://github.com/Mutaracha/REAL/releases/latest). Nothing has to be
   installed, and no Visual C++ Redistributable is needed. Requirements: Windows 10 or
   Windows 11, 64-bit.
2. Put `REAL.exe` into a folder of its own that you can write to, for example `C:\Tools\REAL`.
   REAL keeps its settings file `real.settings.json` next to itself, and the log file and the
   diagnostics report when you ask for them. Avoid `C:\Program Files`: the settings cannot be
   saved there.
3. Start `REAL.exe`. The file is not signed, so Windows SmartScreen may show
   **Windows protected your PC**: click **More info**, then **Run anyway**. The browser may
   also warn that the file is not commonly downloaded.
4. The status bar at the bottom of the window shows the buffer in use, for example
   `2.67 ms - Speakers (Realtek(R) Audio)`. If it says that the driver already uses its
   smallest buffer, see [Limitations](#11-limitations) and the next section.
5. To start REAL together with Windows, turn on **Options → Start with Windows**.

The latency stays low as long as REAL is running; the window can be hidden in the tray.

### The standard Windows driver (recommended)

The drivers of sound card vendors often offer nothing smaller than 10 ms. The standard
Windows driver usually supports smaller buffers for on-board sound:

1. Open **Device Manager**.
2. Under **Sound, video and game controllers**, double-click the device of your speakers.
3. On the **Driver** tab click **Update driver** → **Browse my computer for drivers**
   (Windows 10: **Browse my computer for driver software**) →
   **Let me pick from a list of available drivers on my computer**.
4. Select **High Definition Audio Device** and click **Next**. If the list does not have it,
   clear **Show compatible hardware**.
5. Restart the computer if Windows asks for it.

> **Careful:** after the change the volume may become uncomfortably loud. To go back, pick
> the previous driver the same way.

## 3. The window

The main window shows the journal: what REAL does — the start, the settings file, the devices,
restarts and errors. The journal shows the main events whatever the log file settings are.

The status bar at the bottom shows the current state:

| Status bar | Meaning |
|---|---|
| `2.67 ms - Speakers (Realtek(R) Audio)` | The buffer the audio engine uses now and the device. `(+1 more)` — one more device is handled |
| `… (buffer set by another app)` | Another program had already set the buffer; REAL uses it (see [Limitations](#11-limitations)) |
| `driver already uses its smallest buffer (10.00 ms) - …` | The driver of the device has nothing smaller, REAL keeps no stream there |
| `Latency reduction is off` | Switched off in the menu |
| `disabled: the device does not answer` | The device did not answer in time (see [Automatic restart](#6-automatic-restart)) |
| `Latency reduction is not active` | Nothing is applied, for example there is no default device |

The menu bar:

* **REAL**: **REAL is running** — the check mark switches the latency reduction on and off;
  **Restart** — the audio streams are created again; **Exit**.
* **Options**: **Settings** — the settings window; **Start with Windows**.
* **Diagnostics**: **Diagnostics** — a report about the audio devices (see
  [section 10](#10-diagnostics-and-reporting-a-problem)); **Open the log** — the log file, when it is on.
* **About**: the version, the path of the settings file and links to the project and to this
  documentation.

The buttons of the window:

* **Minimize** hides the window in the tray (the setting **Minimize to tray**).
* **Close** exits REAL. With **Close button: Minimize to tray** it hides the window instead.
* Without the tray icon the window has nowhere to hide: Minimize goes to the taskbar, Close
  always exits.

Only one copy of REAL runs at a time. Starting `REAL.exe` again brings up the window of the
running copy.

## 4. The tray icon

* Left click shows or hides the window, right click opens the menu.
* The tooltip shows the state: `REAL - 2.67 ms - Speakers (Realtek(R) Audio)`.
* The menu: the state line, **REAL is running**, **Restart** — **Log file**, **Diagnostics** —
  **Settings** — **Exit**. **Log file** and **Diagnostics** are hidden by default (they are in the
  menu bar of the window); the items can be shown or hidden in the settings, the state line,
  **Settings** and **Exit** are always there.
* The icon comes back after Windows Explorer is restarted.

## 5. Settings

**Options → Settings** (or **Settings** in the tray menu) opens the **REAL settings** window
with three tabs:

* **Window** — the language, the start, the buttons of the window, the tray icon and its menu,
  notifications;
* **Audio** — the devices, the buffer, the automatic restart;
* **Other** — the priority of the process, the update check, the log file, the settings file.

**Save** writes the settings file and applies the values at once, nothing has to be restarted.
**Cancel** (or Esc) closes the window without changes. Hover over a setting to see a short
description. The values are checked when you click **Save**: a value that does not fit is
put back, the window opens the tab of the field and a tip next to it names the values it
accepts.

The settings live in `real.settings.json` next to `REAL.exe`. The window and the file are the
same thing: what the window saves is in the file, with a comment next to every value.
The file can be edited by hand too: **Other → Settings file → Open** opens it in the editor,
**Reload** reads it into the window again. A click on the path copies the path of the folder.
Every setting is described in the [settings reference](settings.en.md).

**Language** (tab **Window**): **As in Windows**, **English** or **Русский**. The settings window
switches at once; the rest of the program and the comments in the file — after **Save**.
**As in Windows** gives Russian for Russian, Ukrainian and Belarusian Windows, English otherwise.

## 6. Automatic restart

REAL creates the audio streams again by itself, without restarting the program (tab **Audio**,
group **Automatic restart**):

* **Default device change** — another device became the default one or the default
  communication device. Windows reports a default device that is disconnected or plugged back
  in the same way. Other devices do not cause a restart: a microphone while playback is
  handled, the sound of a monitor that goes to sleep, headphones that did not become the
  default device.
* **Resume from sleep**, Modern Standby included.
* **Session unlock** (after Win+L).
* In addition, every 30 seconds REAL checks that the stream is alive and the device is still
  the default one.

Windows sends a burst of events for one change, so REAL waits for **Pause before restarting, s**
(1 s by default) after the last one.

If the device does not answer, REAL tries again after 2, 4, 8 … up to 30 seconds. After
**Wait for the device, s** (60 s by default) it switches the latency reduction off and stops
polling; the status bar says `disabled: the device does not answer`. The next default device
change switches it on again.

If there is no default device at all (for example, the sound is not ready yet right after
Windows starts), the journal says `No default audio device is connected.` As soon as a device
appears, the latency reduction is applied.

A default device change switches the latency reduction on even when it was off — by you or
because the device did not answer.

**Restart** in the menu (or `REAL.exe --reinit`) does the same by hand, and switches the latency
reduction on when it was off.

## 7. Notifications

Notifications come without a sound and only while the window is hidden in the tray or
minimized: while it is open, the journal says the same. Each kind can be switched off in the
settings (tab **Window**, group **Notifications**):

* **Notify about errors** — the default device stopped answering: one notification per outage,
  not per attempt, and one when the sound works again. No device at all is not an error: only
  a line in the journal.
* **Notify about a default device change** — Windows switched to another default device and
  REAL restarted the streams on it.
* **Notify when switched on or off** — the latency reduction was switched on or off in the
  menu, switched on by itself after a default device change, or restarted with **Restart**.

Some notifications have no setting: a new version found at startup (a click opens the
release page), a settings file that could not be read, and the answers to menu commands
(for example, **Log file** while the log file is off).

## 8. Updates

* REAL checks for a new version once at startup, with one request to
  `github.com/Mutaracha/REAL/releases/latest`. There are no requests while it runs.
* **Other → Updates → Check at startup** turns the check off: then REAL makes no network
  requests at all.
* A new version is shown as a line in the journal, `REAL v1.0.1 is available (…)`, and as a
  notification while the window is hidden. A click on the notification opens the release
  page. Nothing is downloaded or installed, and REAL is never closed for an update.
* A check that finds nothing new, or fails because there is no network, is written to the
  log file only.

To update, exit REAL and replace `REAL.exe` with the new one. The settings stay.

## 9. Command line

| Option | What it does |
|---|---|
| *(none)* | Start with the window (and the tray icon when it is on) |
| `--tray` | Start minimized to the tray |
| `--no-tray` | Start with the window visible |
| `--config <path>` | Use another settings file instead of `real.settings.json` |
| `--no-config` | Do not read the settings file, use the built-in values |
| `--log-level <level>` | Log file level: `off`, `error`, `warn`, `info`, `debug`, `trace` |

`--tray`, `--no-tray` and `--log-level` hold for that start only and are never written to the
settings file.

Commands for the running copy: the command is passed to it, and the new process exits.

| Option | What it does |
|---|---|
| `--reinit` | Restart: create the audio streams again; the latency reduction is switched on |
| `--enable` | Switch the latency reduction on |
| `--disable` | Switch the latency reduction off |
| `--exit` | Close the running copy |

Commands that run on their own:

| Option | What it does |
|---|---|
| `--diagnose` | Write a report about the audio devices to `REAL-diagnostics.txt` |
| `--help`, `-h`, `/?` | Show the help |
| `--version` | Show the version |

Options are not case-sensitive. An unknown option is reported in the journal and ignored.
Started from a command prompt, `--help`, `--version` and `--diagnose` print their text there;
otherwise the text is shown in a message box (the report is opened in the text editor).

## 10. Diagnostics and reporting a problem

**Diagnostics → Diagnostics** in the menu bar (or `REAL.exe --diagnose`) writes
`REAL-diagnostics.txt` next to `REAL.exe` and opens it. If the folder is read-only, the report
goes to the temporary folder (`%TEMP%`). The report lists:

* the versions of REAL and Windows, the scale of the interface, the processor cores and the
  cores Windows reserved for audio;
* the settings in use;
* every active device of the direction chosen in the settings: the driver and its version,
  the format and the buffer sizes the device supports — default, minimum, base step and
  maximum — with a conclusion: whether the latency can be lowered on this device.

The log file is off by default. To trace a problem:

1. **Options → Settings → Other → Log → Level**: choose **Detailed (debug)** and click **Save**.
2. Repeat the problem.
3. **Diagnostics → Open the log** opens `REAL.log`.
4. Afterwards set the level back to **Off (off)**.

To report a problem, open an [issue](https://github.com/Mutaracha/REAL/issues) and attach
`REAL-diagnostics.txt` and, if you have it, `REAL.log`. Write the version of REAL (the **About**
window or `REAL.exe --version`), the version of Windows and what happened. The report contains
the names of your audio devices and the paths of `REAL.exe` and of the settings file (they may
contain your user name); remove what you do not want to share.

The version tells which build you have: a release shows `v1.0.0`; a test build from GitHub
Actions shows `v1.0.0 RC 71 (0f7eb3a)` — the number of the build and the commit it was made
from.

## 11. Limitations

* **The driver decides.** Bluetooth (10 ms by design), HDMI and DisplayPort receivers, virtual
  devices and some vendor drivers offer nothing smaller than their default buffer. REAL keeps
  no stream on such a device, and the status bar says
  `driver already uses its smallest buffer (10.00 ms) - …`. For on-board sound the standard
  Windows driver often helps (see [section 2](#the-standard-windows-driver-recommended)).
* **The buffer may already be taken.** When another program (a game, a music program) has
  already set the buffer of the engine, Windows does not let it change while that program
  runs. REAL then uses that buffer, and the status bar adds `(buffer set by another app)`.
  After that program exits, **Restart** brings back the buffer of REAL.
* **Default devices only.** A program that plays through another device does not get the
  lower latency.
* **Only while REAL runs.** REAL changes nothing permanently: once it exits, the buffer goes
  back to the default one.
* **Windows 10 or 11, 64-bit.** The interface is in English or Russian.

## 12. Uninstalling

1. Turn off **Options → Start with Windows**: this removes the autostart entry `REAL` (in
   `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`).
2. Exit REAL: **REAL → Exit** or **Exit** in the tray menu.
3. Delete the folder with `REAL.exe`. Besides the program it may contain `real.settings.json`,
   `REAL.log` with its older copies (`REAL.1.log` …), `REAL-diagnostics.txt` and
   `real.settings.json.bak` or `.bad`. A report written to `%TEMP%` stays there.

REAL installs nothing else: no services, drivers or other registry entries. The standard
Windows driver, if you installed it, stays; to go back, pick the previous driver in Device
Manager.
