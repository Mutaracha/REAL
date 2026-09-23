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

// Main application window: shows the current status and the log, and hosts the
// tray icon. Hiding the window keeps the application (and the audio stream)
// running in the tray.
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
    void DestroyResources();

    HWND m_window = nullptr;
    HINSTANCE m_instance = nullptr;

    HWND m_status = nullptr;
    HWND m_log = nullptr;
    std::vector<std::pair<HWND, miniant::Command>> m_buttons;

    HFONT m_uiFont = nullptr;
    HFONT m_monoFont = nullptr;

    std::unique_ptr<TrayIcon> m_tray;
    UINT m_taskbarCreatedMessage = 0;

    CommandHandler m_commandHandler;
    MessageHandler m_messageHandler;

    bool m_minimizeToTray = true;
    bool m_hideOnClose = true;
    bool m_trayEnabled = false;
};

}
