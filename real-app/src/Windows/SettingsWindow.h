#pragma once

#include <Windows.h>

#include "../Settings.h"

#include <string>

namespace miniant::Windows {

// The settings window: every parameter that a user changes is edited here
// instead of the file. It is modal for the main window, and it returns true
// when the user pressed "Save" - the caller then writes the file and applies
// the new settings (see App::ShowSettingsWindow), so the file stays the single
// source of truth and the comments in it are written by the same code.
bool ShowSettingsWindow(
    HWND owner,
    HINSTANCE instance,
    Config::Settings& settings,
    const std::wstring& settingsPath);

// Brings the settings window of this program to the front when it is open
// (the tray menu can ask for it while the window is hidden behind others).
void ActivateSettingsWindow();

}
