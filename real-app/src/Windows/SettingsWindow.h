#pragma once

#include <Windows.h>

#include "../Settings.h"

#include <cstdint>
#include <string>

namespace miniant::Windows {

// The buffer range of the device REAL works with (the default device of the
// last apply): the settings window shows it under the field of the fixed
// buffer and checks the value against it. A zero minimum means that no device
// has been seen yet; the window then checks only that the value is a number.
struct BufferRange {
    std::wstring deviceName;
    uint32_t sampleRate = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    uint32_t step = 0;
    // The buffer the device runs with now: a fixed buffer starts from it.
    uint32_t current = 0;
};

// The settings window: every parameter that a user changes is edited here
// instead of the file. It is modal for the main window, and it returns true
// when the user pressed "Save" - the caller then writes the file and applies
// the new settings (see App::ShowSettingsWindow), so the file stays the single
// source of truth and the comments in it are written by the same code.
bool ShowSettingsWindow(
    HWND owner,
    HINSTANCE instance,
    Config::Settings& settings,
    const std::wstring& settingsPath,
    const BufferRange& bufferRange);

// Brings the settings window of this program to the front when it is open
// (the tray menu can ask for it while the window is hidden behind others).
void ActivateSettingsWindow();

}
