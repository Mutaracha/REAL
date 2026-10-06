#pragma once

#include <Windows.h>

namespace miniant::Windows {

// Where a secondary window of the program has to open: a little to the right
// and below the main window, so that it does not fly away to the middle of the
// screen. The position is pulled back into the work area of the monitor, and
// the default position of Windows is used when there is no main window (a
// minimised program, a start without a window).
void PlaceNearOwner(HWND owner, int width, int height, int& x, int& y);

}
