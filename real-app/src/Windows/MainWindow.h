#pragma once

#include "../Commands.h"
#include "TrayIcon.h"
#include "WindowsError.h"

#include <tl/expected.hpp>

#include <Windows.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace miniant::Windows {

// Main application window: shows the log and the current status (with the
// restart button at the end of the status line), and hosts the tray icon.
// Hiding the window keeps the application (and the audio stream) running in
// the tray.
class MainWindow {
public:
    using CommandHandler = std::function<void(miniant::Command)>;
    using MessageHandler = std::function<void(UINT message, WPARAM wParam, LPARAM lParam)>;

    static tl::expected<std::unique_ptr<MainWindow>, WindowsError> Create(HINSTANCE instance);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND GetHWindow() const;
    HINSTANCE GetInstance() const;

    void SetCommandHandler(CommandHandler handler);
    void SetMessageHandler(MessageHandler handler);

    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const;

    void SetMinimizeToTray(bool value);
    void SetHideOnClose(bool value);

    // Re-reads every caption from the language table (used at start and after
    // the language setting has changed).
    void ApplyLanguage();

    // Marks the menu items that show a state: the latency reduction and the
    // autostart. The labels themselves come from the language table.
    void SetMenuChecks(bool latencyEnabled, bool startWithWindows);

    void SetStatusText(const std::wstring& text);
    void AppendLogLines(const std::vector<std::string>& lines);
    void Notify(const std::wstring& title, const std::wstring& text, bool error);
    void SetTrayTooltip(const std::wstring& text);
    void SetTrayMenuState(const TrayMenuState& state);
    void EnableTray(bool enabled);
    bool IsTrayVisible() const;

    void RaiseCommand(miniant::Command command);

private:
    MainWindow(HINSTANCE instance);

    static LRESULT CALLBACK WindowProcedureThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProcedure(UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void LayoutControls();
    void CreateFonts();
    void ApplyFonts();
    void ApplyDpi(UINT dpi);
    void ResizeToDesignSize();
    // A design pixel (96 DPI) in the pixels of the monitor the window is on.
    int S(int value) const;
    void DestroyResources();

    HWND m_window = nullptr;
    HINSTANCE m_instance = nullptr;

    HMENU m_menu = nullptr;
    HMENU m_programMenu = nullptr;
    HMENU m_optionsMenu = nullptr;
    HMENU m_diagnosticsMenu = nullptr;
    bool m_latencyEnabled = true;
    bool m_startWithWindows = false;

    HWND m_status = nullptr;
    HWND m_log = nullptr;
    UINT m_dpi = 96;
    HWND m_restartButton = nullptr;
    HWND m_tooltip = nullptr;
    bool m_statusTextSet = false;

    HFONT m_uiFont = nullptr;
    HFONT m_monoFont = nullptr;
    HFONT m_iconFont = nullptr;

    std::unique_ptr<TrayIcon> m_tray;
    UINT m_taskbarCreatedMessage = 0;

    CommandHandler m_commandHandler;
    MessageHandler m_messageHandler;

    bool m_minimizeToTray = true;
    bool m_hideOnClose = true;
    bool m_trayEnabled = false;
};

}
