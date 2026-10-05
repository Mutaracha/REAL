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

// The class of the main window: a second start of the program finds the
// running copy by it (see SignalRunningInstance).
inline constexpr wchar_t MAIN_WINDOW_CLASS_NAME[] = L"REAL.MainWindow";

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
    // A balloon of the tray icon; nothing is shown while the window is open
    // (visible and not minimized), its journal already has the same line.
    // True when the balloon has been handed to the tray.
    bool Notify(const std::wstring& title, const std::wstring& text, bool error);
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
    // Creates the fonts for the current DPI; the fonts they replace are
    // deleted by the caller once the controls have the new ones.
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
