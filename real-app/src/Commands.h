#pragma once

namespace miniant {

// Commands that can be triggered from the main window, the tray menu, a global
// hotkey, the command line or another instance of the application.
enum class Command {
    ToggleEnabled,
    Reinitialize,
    OpenSettings,
    OpenLog,
    CheckForUpdates,
    ToggleStartWithWindows,
    ReloadSettings,
    ShowWindow,
    ToggleWindow,
    BalloonClicked,
    About,
    HideToTray,
    Exit,
};

}
