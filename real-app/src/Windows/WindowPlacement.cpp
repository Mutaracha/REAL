#include "WindowPlacement.h"

const int OWNER_OFFSET = 40;

void miniant::Windows::PlaceNearOwner(HWND owner, int width, int height, int& x, int& y) {
    x = CW_USEDEFAULT;
    y = CW_USEDEFAULT;

    if (owner == nullptr || ::IsWindow(owner) == FALSE || ::IsIconic(owner) != FALSE) {
        return;
    }

    RECT ownerRect = {};
    if (::GetWindowRect(owner, &ownerRect) == FALSE) {
        return;
    }

    RECT work = ownerRect;

    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);

    const HMONITOR handle = ::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    if (handle != nullptr && ::GetMonitorInfoW(handle, &monitor) != FALSE) {
        work = monitor.rcWork;
    }

    int left = ownerRect.left + OWNER_OFFSET;
    int top = ownerRect.top + OWNER_OFFSET;

    // The window has to be fully visible: it is moved back inside the working
    // area of the monitor the main window is on.
    if (left + width > work.right) {
        left = work.right - width;
    }

    if (top + height > work.bottom) {
        top = work.bottom - height;
    }

    if (left < work.left) {
        left = work.left;
    }

    if (top < work.top) {
        top = work.top;
    }

    x = left;
    y = top;
}
