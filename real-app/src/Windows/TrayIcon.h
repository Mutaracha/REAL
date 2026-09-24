#pragma once

#include "../Commands.h"
#include "WindowsError.h"

#include <tl/expected.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

namespace miniant::Windows {

struct TrayMenuState {
    bool enabled = true;
    bool showStatus = true;
    std::wstring statusText;

    bool toggleEnabled = true;
    bool reinitialize = true;
    bool openSettings = true;
    bool openLog = true;
    bool diagnostics = true;
    bool startWithWindows = true;
    bool startWithWindowsChecked = false;
    bool about = true;
    bool exit = true;
};

// System tray icon with a context menu, tooltip and balloon notifications.
class TrayIcon {
public:
    using CommandHandler = std::function<void(miniant::Command)>;

    TrayIcon(HWND owner, UINT callbackMessage, HICON icon);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    tl::expected<void, WindowsError> Show();
    void Hide();
    bool IsVisible() const;

    void SetTooltip(const std::wstring& text);
    void SetMenuState(const TrayMenuState& state);
    void SetCommandHandler(CommandHandler handler);

    void Notify(const std::wstring& title, const std::wstring& text, bool error);

    // Called from the window procedure for the tray callback message.
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    // Called when "TaskbarCreated" arrives (the shell has been restarted).
    void Recreate();

private:
    void ShowContextMenu(const POINT& anchor);

    HWND m_owner;
    NOTIFYICONDATAW m_data = {};
    TrayMenuState m_state;
    CommandHandler m_handler;
    bool m_visible = false;
    ULONGLONG m_lastToggleTick = 0;
};

}
