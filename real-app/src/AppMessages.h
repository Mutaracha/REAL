#pragma once

#include <Windows.h>

namespace miniant {

// Custom messages used by the application window.
enum : UINT {
    WM_APP_LOG_LINES = WM_APP + 1,
    WM_APP_DEVICE_EVENT = WM_APP + 2,
    WM_APP_UPDATE_RESULT = WM_APP + 3,
    WM_APP_TRAY = WM_APP + 4,
};

// Ids of the global hotkeys.
enum : int {
    HOTKEY_ID_TOGGLE = 1,
    HOTKEY_ID_REINITIALIZE = 2,
};

}
