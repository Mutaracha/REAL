#pragma once

#include <Windows.h>

namespace miniant::Windows {

// One copy of the program per user session: the copy that runs owns a named
// mutex until it ends, and a second start finds the mutex taken.
class SingleInstance {
public:
    enum class State {
        // This process owns the mutex: it is the copy that runs.
        First,
        // Another copy owns it.
        AnotherRunning,
        // The mutex could not be created (see Error()): another copy cannot be
        // detected.
        Unknown,
    };

    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    State Acquire(const wchar_t* name);

    // Waits until the other copy has ended: it gives the mutex back, or the
    // system does it for a copy that crashed. True when this process owns the
    // mutex now.
    bool WaitForOwnership(DWORD timeoutMs);

    // Gives the mutex back, so that the next start becomes the running copy.
    void Release();

    // The error of a failed Acquire().
    DWORD Error() const;

private:
    HANDLE m_mutex = nullptr;
    bool m_owned = false;
    DWORD m_error = 0;
};

// The result of a command sent to the window of the running copy.
enum class SignalResult {
    Delivered,
    // The window is there but has not taken the message in time.
    NotResponding,
    // There is no such window: the copy is ending, or it is still starting.
    NoWindow,
};

// Sends a registered message straight to the main window of the running copy
// (never to every window of the system) and waits until the window has
// processed it, at most timeoutMs.
SignalResult SignalRunningInstance(const wchar_t* windowClass, UINT message, UINT timeoutMs);

}
