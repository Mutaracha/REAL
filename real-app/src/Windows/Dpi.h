#pragma once

#include <Windows.h>

namespace miniant::Windows::Dpi {

// The dots per inch of the monitor a window is on. A window that does not exist
// yet (or a system without per-monitor DPI) gives the DPI of the desktop.
UINT ForWindow(HWND window);

// The dots per inch of the desktop.
UINT ForSystem();

// A design pixel (the layout is written for 96 DPI) in the pixels of the given
// DPI: MulDiv rounds the way the system does, so the numbers do not drift.
int Scale(int value, UINT dpi);

// The frame of a window for the client area in the pixels of the given DPI;
// a window with a menu bar gets the bar added as well.
void AdjustWindowRect(RECT& rect, DWORD style, UINT dpi, bool hasMenu = false);

// The fonts of the interface, for the given DPI: the message font of the system
// (so that the user's font size and face are respected) and a monospaced font
// for the log.
HFONT CreateUiFont(UINT dpi);
HFONT CreateMonoFont(UINT dpi);

// A name in the About window: the message font, bold and twice as high.
HFONT CreateNameFont(UINT dpi);

// A link in the About window: the message font, underlined.
HFONT CreateLinkFont(UINT dpi);

}
