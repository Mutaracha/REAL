#pragma once

namespace miniant {

// Commands that can be triggered from the main window, the tray menu, the
// command line or another instance of the application.
enum class Command {
    ToggleEnabled,
    Reinitialize,
    OpenSettings,
    OpenLog,
    Diagnose,
    ToggleStartWithWindows,
    ShowWindow,
    ToggleWindow,
    BalloonClicked,
    About,
    Exit,
};

}
