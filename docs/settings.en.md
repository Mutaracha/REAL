# REAL — settings reference

[Русская версия](settings.ru.md) · [User guide](usage.en.md)

Every setting of REAL: its key in `real.settings.json`, its place in the settings window, the
values it takes, its default value and what it does.

## Contents

1. [The settings file](#1-the-settings-file)
2. [When the file cannot be used](#2-when-the-file-cannot-be-used)
3. [Language](#3-language)
4. [application](#4-application)
5. [tray](#5-tray)
6. [audio](#6-audio)
7. [performance](#7-performance)
8. [updates](#8-updates)
9. [logging](#9-logging)
10. [Service fields](#10-service-fields)

## 1. The settings file

* `real.settings.json` lies next to `REAL.exe` and is created at the first start with the
  default values. The **About** window shows its path. To return to the defaults, exit REAL
  and delete the file: the next start creates it again.
* The format is JSON with comments: `//` and `/* */` are allowed, the encoding is UTF-8 (a BOM
  is accepted). A comma after the last element of a list or an object is not allowed.
* Every value has a comment on the same line, in the language of the interface; the first
  lines of the file link to this page.
* The settings window (**Options → Settings**) edits the same file: what it saves is in the
  file. At startup REAL rewrites the file only when the language of its comments or its layout
  has changed (see [Service fields](#10-service-fields)), so your own edits stay as they are.
  Saving from the window writes the file anew: your own comments and unknown keys are not kept.
* The file is written through a temporary file, so an interrupted write cannot leave half a
  file behind.
* After a change by hand, start REAL again, or click **Reload** in the settings window (tab
  **Other**, group **Settings file**) and then **Save**. **Open** in the same group opens the
  file in the editor.

Where the values come from, in order of priority:

```
command line  >  real.settings.json  >  built-in default values
```

* `--config <path>` reads and writes another file instead of `real.settings.json`.
* `--no-config` does not read the file: the built-in values are used.
* `--tray`, `--no-tray` and `--log-level` hold for one start and are never written to the file.

## 2. When the file cannot be used

| Situation | What REAL does |
|---|---|
| There is no file | Creates it with the default values |
| The file cannot be parsed (an extra comma, a missing quote) | Works with the default values and does not change the file. The journal names the reason, for example the place of the error; one notification, if the window is hidden. When the settings are saved, the old text is kept as `real.settings.json.bad` |
| A value cannot be used: a wrong type, a number out of range, an unknown word | The value is not applied: the last value in use is kept — the default one at startup, the current one after **Reload**. The journal names the key, the reason and the value that is used: `audio.reinit.debounceSec: 70 is out of the range 1–60; using 1` |
| An unknown key | Ignored with a line in the journal: `application.example: unknown option, ignored`. It disappears at the next save |
| The file is larger than 100 KB | It is not a settings file and is not read. At startup it is renamed to `real.settings.json.bak` (an older `.bak` is replaced), and a new file with the default values takes its place; the journal says so |
| The file is larger than 100 KB and read-only | Left as it is: REAL works with the default values, and the settings cannot be saved while the file is larger than 100 KB |

The 100 KB check runs before every write of the file as well: saving the settings, **Start with
Windows**, rewriting the comments. **Reload** in the settings window does not rename the file,
it only names the reason.

The settings window checks the values when you click **Save**, with the same limits as the
reader of the file: a value the window has accepted is never rejected at the next start.

## 3. Language

`application.language` sets the language of everything at once: the interface, the journal,
the log file, the notifications and the comments in the settings file.

* `"auto"` (default) — the language of Windows: Russian, Ukrainian and Belarusian give Russian,
  any other language gives English;
* `"en"` — English, `"ru"` — Russian.

Case does not matter; `"english"` and `"russian"` are understood too.

When the language changes, the file is rewritten once with comments in the new language, and
every value is kept: after **Save** in the settings window, or at the next start after an edit
by hand. With `"auto"` the same happens when the language of Windows changes. The settings
window itself switches to the chosen language at once; the rest of the program follows after
**Save**, **Cancel** keeps the previous language.

## 4. application

Tab **Window**, group **Application**.

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `language` | **Language** | `"auto"` (**As in Windows**), `"en"` (**English**), `"ru"` (**Русский**) | `"auto"` | The language of the program and of the comments in this file, see [Language](#3-language) |
| `startWithWindows` | **Start with Windows** | `true`, `false` | `false` | REAL starts when you sign in to Windows: the entry `REAL` in `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. The same as **Options → Start with Windows** in the menu |
| `startMinimizedToTray` | **Start minimized to tray** | `true`, `false` | `false` | At startup the window stays closed and REAL works in the tray, as with `--tray` |
| `minimizeToTray` | **Minimize to tray** | `true`, `false` | `true` | The Minimize button hides the window in the tray instead of the taskbar |
| `closeButtonAction` | **Close button** | `"exit"` (**Exit**), `"minimize"` (**Minimize to tray**) | `"exit"` | What the close button of the window does |

The last three need the tray icon. Without it they are grey in the window and do not apply: the
window is visible at startup, Minimize goes to the taskbar and the close button exits.

The autostart entry follows the file: REAL creates, corrects (for example, after the folder has
been moved) or removes it at startup.

## 5. tray

Tab **Window**, groups **Tray icon** and **Notifications**.

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `enabled` | **Show the tray icon** | `true`, `false` | `true` | The icon in the notification area: a left click shows or hides the window, a right click opens the menu |
| `menu.toggleEnabled` | **Icon menu items:** **REAL is running** | `true`, `false` | `true` | The menu item that switches the latency reduction on and off |
| `menu.reinitialize` | **Icon menu items:** **Restart** | `true`, `false` | `true` | The menu item that creates the audio streams again |
| `menu.openLog` | **Icon menu items:** **Log file** | `true`, `false` | `false` | The menu item that opens the log file |
| `menu.diagnostics` | **Icon menu items:** **Diagnostics** | `true`, `false` | `false` | The menu item that writes the diagnostics report |
| `notifications.onError` | **Notify about errors** | `true`, `false` | `true` | The default device stopped answering: one notification per outage, not per attempt, and one when the sound works again |
| `notifications.onDeviceChange` | **Notify about a default device change** | `true`, `false` | `true` | Windows switched to another default device and REAL restarted the audio streams on it |
| `notifications.onStateChange` | **Notify when switched on or off** | `true`, `false` | `true` | The latency reduction was switched on or off in the menu, switched on by itself after a default device change, or restarted with **Restart** |

* The state line, **Settings** and **Exit** are always in the tray menu. **Log file** and
  **Diagnostics** are in the menu bar of the window as well.
* Notifications come without a sound and only while the window is hidden in the tray or
  minimized. The start of REAL and the restarts after sleep and unlock bring no notification
  about the state; no default device at all is not an error either, only a line in the journal.
* Without the tray icon the menu items and the notifications are grey in the window: the icon
  shows them.

## 6. audio

Tab **Audio**, groups **Audio streams** (the first three keys) and **Automatic restart** (the
keys of `audio.reinit`).

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `dataFlow` | **Devices** | `"render"` (**Playback**), `"capture"` (**Recording**), `"both"` (**Playback and recording**) | `"render"` | Which default devices get the lower latency. In each direction every default device is handled, the communication device included |
| `buffer` | **Buffer** | `"min"` (**Minimum**), `"fixed"` (**Fixed**) | `"min"` | The smallest buffer the driver supports, or the size from `fixedBufferFrames` |
| `fixedBufferFrames` | **Fixed buffer, frames** | 0–100000 | `0` | The size of the fixed buffer in frames, for `"fixed"` only; 0 — not set |
| `reinit.defaultDeviceChanged` | **Default device change** | `true`, `false` | `true` | Restart when another device becomes the default one or the default communication device; Windows reports a default device that is disconnected or plugged back in the same way |
| `reinit.resumeFromSleep` | **Resume from sleep** | `true`, `false` | `true` | Restart after sleep or Modern Standby |
| `reinit.sessionUnlock` | **Session unlock** | `true`, `false` | `true` | Restart after the session is unlocked (Win+L) |
| `reinit.failureTimeoutSec` | **Wait for the device, s** | 5–300 | `60` | How long to retry a device that does not answer; then the latency reduction is switched off and the device is not polled anymore |
| `reinit.debounceSec` | **Pause before restarting, s** | 1–60 | `1` | Windows sends a burst of events for one change: the restart waits this long after the last one |

How the restart works — the retries, the check every 30 seconds, what switches the latency
reduction back on — is described in the
[user guide](usage.en.md#6-automatic-restart).

### The buffer

The buffer of the audio engine is measured in frames: one frame is one sample of every channel,
so at 48,000 Hz 480 frames are 10 ms. The fewer frames, the lower the latency.

The driver reports the buffers it supports: default, minimum, base step and maximum. The
diagnostics report shows them, and the settings window shows the range of the default device
under the name of the field, for example `Allowed: 144–480, step 48 (3.00–10.00 ms)`. A fixed
buffer has to lie between the minimum and the maximum and be a multiple of the step. The base
step is not a buffer size: it may be smaller than the minimum (even 1 frame), and REAL never asks
for less than the minimum.

* The window accepts only a value that fits the current default device. Until the device has
  been started at least once and its range is unknown, the window checks only that the value
  is a whole number from 1 to 100000. Choosing **Fixed** with an empty field puts the buffer the
  device uses now into it.
* A value from the file that the device does not accept (the file was edited by hand, or the
  device has changed) is adjusted — rounded down to the step and kept within the range — and
  the journal says so once: `The fixed buffer of 500 frames does not fit the device "Speakers"
  (144–480, step 48), using 480 frames.`
* `"fixed"` with `fixedBufferFrames: 0` gives a warning when the file is read, and the minimum
  buffer is used.

## 7. performance

Tab **Other**, group **REAL process**.

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `processPriority` | **Process priority** | `"idle"` (**Low**), `"belowNormal"` (**Below normal**), `"normal"` (**Normal**) | `"idle"` | The priority of the REAL process, as in the Task Manager |

The priority does not affect the sound: the sound is processed by the Windows Audio service,
and REAL only holds the stream and waits for events. For the same reason Windows 11 always
treats the REAL process as an efficient one (EcoQoS), without a setting.

## 8. updates

Tab **Other**, group **Updates**.

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `checkOnStartup` | **Check at startup** | `true`, `false` | `true` | One check for a new version at startup; `false` — no network requests at all |

The releases are read from the repository of the project, fixed in the program. There are no
checks while REAL runs, nothing is downloaded or installed. Details are in the
[user guide](usage.en.md#8-updates).

## 9. logging

Tab **Other**, group **Log**. The section is about the log file only: the journal in the window
always shows the main events, whatever the level of the file is.

| Key | In the window | Values | Default | What it does |
|---|---|---|---|---|
| `level` | **Level** | `"off"` (**Off (off)**), `"error"` (**Errors (error)**), `"warn"` (**Warnings (warn)**), `"info"` (**Main events (info)**), `"debug"` (**Detailed (debug)**), `"trace"` (**Everything (trace)**) | `"off"` | `off` — no log file at all; the other levels write the file, from the shortest to the most detailed. `none`, `err` and `warning` are understood too |
| `filePath` | **File path** | a path | `"REAL.log"` | The log file: a path relative to the folder of `REAL.exe` or a full path. The window does not accept an empty path or the characters `< > " \| ? *` |
| `maxFileSizeMb` | **File size, MB** | 1–1024 | `1` | When the file reaches this size, a new one is started |
| `maxFiles` | **Files to keep** | 1–100 | `3` | How many log files to keep; the oldest one is deleted when a new one starts |

Every line of the file starts with the date, the time and the level:
`[2026-10-01 12:00:00.000] [info] …`. **Diagnostics → Open the log** in the menu bar (or **Log
file** in the tray menu) opens the file. `--log-level <level>` sets the level for one start.

## 10. Service fields

Two fields at the top of the file, without comments. They do not need to be changed by hand.

| Key | Value | What it does |
|---|---|---|
| `configVersion` | `1` | The layout of the file. A file with another number is rewritten once in the current layout: the values the program knows are kept, the keys it does not know disappear |
| `commentLanguage` | `"en"` or `"ru"` | The language of the comments in the file. When it differs from the language of the program, the file is rewritten with comments in the current language, and every value is kept |
