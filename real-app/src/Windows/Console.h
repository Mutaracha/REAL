#pragma once

#include <string>

namespace miniant::Windows {

// Optional console that mirrors the log output.
//
// Unlike the previous implementation, closing the console window does not send
// WM_CLOSE to conhost (which terminates the process); the window is only hidden
// or shown. Ctrl+C / Ctrl+Break are swallowed so that an accidental key press in
// the console does not stop the latency reduction.
class Console {
public:
    static bool Attach();
    static void Detach();

    static bool IsAttached();
    static bool IsVisible();

    static void Show();
    static void Hide();
    static void Toggle();
};

}
