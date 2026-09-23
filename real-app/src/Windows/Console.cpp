#include "Console.h"

#include <Windows.h>

#include <cstdio>

using namespace miniant::Windows;

namespace {

bool g_attached = false;

BOOL WINAPI ConsoleControlHandler(DWORD eventType) {
    switch (eventType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            // Keep running: the console is only a log mirror.
            return TRUE;

        case CTRL_CLOSE_EVENT:
            // The user closed the console window. Detach from it so that the
            // application (and the latency reduction) keeps running in the tray.
            ::FreeConsole();
            g_attached = false;
            return TRUE;

        default:
            return FALSE;
    }
}

HWND GetConsoleHWindow() {
    return ::GetConsoleWindow();
}

}

bool Console::Attach() {
    if (g_attached) {
        return true;
    }

    // Share the console of the parent process when the application was started
    // from one; only allocate a new console window when there is none.
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        if (::AllocConsole() == FALSE) {
            return false;
        }
    }

    FILE* stream = nullptr;
    ::freopen_s(&stream, "CONOUT$", "w", stdout);
    ::freopen_s(&stream, "CONOUT$", "w", stderr);
    ::freopen_s(&stream, "CONIN$", "r", stdin);

    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    g_attached = true;
    return true;
}

void Console::Detach() {
    if (!g_attached) {
        return;
    }

    ::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    ::FreeConsole();
    g_attached = false;
}

bool Console::IsAttached() {
    return g_attached;
}

bool Console::IsVisible() {
    const HWND window = GetConsoleHWindow();
    return window != nullptr && ::IsWindowVisible(window) != FALSE;
}

void Console::Show() {
    const HWND window = GetConsoleHWindow();
    if (window == nullptr) {
        return;
    }

    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);
}

void Console::Hide() {
    const HWND window = GetConsoleHWindow();
    if (window == nullptr) {
        return;
    }

    ::ShowWindow(window, SW_HIDE);
}

void Console::Toggle() {
    if (IsVisible()) {
        Hide();
    } else {
        Show();
    }
}
