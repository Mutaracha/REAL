#pragma once

#include "../Settings.h"

#include <Windows.h>

namespace miniant::Windows::Theme {

// Follows the setting of the settings file: "auto" means the theme of Windows
// itself, "dark" and "light" force the look of the program windows.
void SetMode(miniant::Config::ThemeMode mode);

// Re-reads the theme of Windows. Called when the system reports a change
// (WM_SETTINGCHANGE with "ImmersiveColorSet"), so that "auto" follows it.
void Refresh();

// True when the program windows have to be drawn dark.
bool IsDark();

// Colours of the program windows and of the controls on them.
COLORREF BackgroundColor();
COLORREF TextColor();
COLORREF MutedTextColor();
COLORREF LinkColor();
COLORREF EditBackgroundColor();

// Brushes for WM_ERASEBKGND and WM_CTLCOLOR*. They are created once and live
// as long as the process.
HBRUSH BackgroundBrush();
HBRUSH EditBackgroundBrush();

// The caption of a window, its scrollbars and its list views. A Windows that
// does not know the dark look keeps its usual one: the colours of the window
// itself are painted by the program in any case.
void ApplyToWindow(HWND window);

// The scrollbars and the list views of a control (an edit box, a list box).
void ApplyToControl(HWND control);

}
