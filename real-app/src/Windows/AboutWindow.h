#pragma once

#include <Windows.h>

#include <string>

namespace miniant::Windows {

// The "About" window: the icon of the program, its name and version, the note
// about what REAL does and the clickable links to the documentation and to the
// project. The window is modal for the main window.
void ShowAboutWindow(HWND owner, HINSTANCE instance, const std::wstring& settingsPath);

}
