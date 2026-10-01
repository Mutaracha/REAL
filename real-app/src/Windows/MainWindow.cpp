#include "MainWindow.h"

#include "Dpi.h"

#include "../../res/resource.h"
#include "../AppMessages.h"
#include "../Lang.h"
#include "../Log.h"
#include "../Text.h"

#include <algorithm>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t WINDOW_CLASS_NAME[] = L"REAL.MainWindow";

const int WINDOW_WIDTH = 780;
const int WINDOW_HEIGHT = 480;

const int STATUS_HEIGHT = 26;
const int BUTTON_HEIGHT = 30;
const int MARGIN = 8;

const size_t MAX_LOG_LENGTH = 200000;

// The bottom row holds the two actions a user needs most: activate the audio
// streams and close the program. The window is hidden to the tray by its close
// button, so a button for that is not needed.
const struct {
    UINT id;
    miniant::Lang::Str text;
    bool left = false;
    miniant::Command command = miniant::Command::Reinitialize;
} BUTTONS[] = {
    { 1, miniant::Lang::Str::ButtonReinitialize, true, miniant::Command::Reinitialize },
    { 3, miniant::Lang::Str::ButtonExit, false, miniant::Command::Exit },
};

// The "File" menu: the states of the program and the exit. The items that open
// a file are in the "Diagnostics" menu, the parameters are in "Options".
const UINT MENU_FILE_TOGGLE_ID = 101;
const UINT MENU_FILE_START_WITH_WINDOWS_ID = 102;
const UINT MENU_FILE_EXIT_ID = 103;

// The sub-menu of "Diagnostics": the report itself and the log file.
const UINT MENU_DIAGNOSTICS_REPORT_ID = 105;
const UINT MENU_DIAGNOSTICS_LOG_ID = 106;

// "Options" and "About" are items of the menu bar itself: the menu of a window
// can hold plain items next to its popups, and a click on one of them sends the
// very same command a drop-down entry would.
const UINT MENU_OPTIONS_ID = 107;
const UINT MENU_ABOUT_ID = 108;

constexpr size_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

const int BUTTON_WIDTH = 190;

}

MainWindow::MainWindow(HINSTANCE instance):
    m_instance(instance) {
    m_taskbarCreatedMessage = ::RegisterWindowMessageW(L"TaskbarCreated");
}

MainWindow::~MainWindow() {
    m_tray.reset();

    if (m_window != nullptr && ::IsWindow(m_window) != FALSE) {
        ::DestroyWindow(m_window);
        m_window = nullptr;
    }

    m_window = nullptr;

    DestroyResources();
    ::UnregisterClassW(WINDOW_CLASS_NAME, m_instance);
}

tl::expected<std::unique_ptr<MainWindow>, WindowsError> MainWindow::Create(HINSTANCE instance) {
    auto result = std::unique_ptr<MainWindow>(new MainWindow(instance));

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &MainWindow::WindowProcedureThunk;
    windowClass.hInstance = instance;
    windowClass.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = WINDOW_CLASS_NAME;

    if (::RegisterClassExW(&windowClass) == 0) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::LogWindowClassFailed)));
    }

    // The window is created with the metrics of the desktop: the monitor it
    // lands on is only known when it exists (WM_DPICHANGED reports a move to
    // another monitor later, and the layout is scaled again).
    const UINT desktopDpi = Dpi::ForSystem();
    RECT desired = { 0, 0, Dpi::Scale(WINDOW_WIDTH, desktopDpi), Dpi::Scale(WINDOW_HEIGHT, desktopDpi) };
    Dpi::AdjustWindowRect(desired, WS_OVERLAPPEDWINDOW, desktopDpi);

    const std::wstring title = miniant::Lang::Wide(miniant::Lang::Str::WindowTitle);

    const HWND window = ::CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        instance,
        result.get());
    if (window == nullptr) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::LogWindowCreateFailed)));
    }

    result->m_window = window;
    result->m_dpi = Dpi::ForWindow(window);
    result->CreateFonts();
    result->CreateControls();
    result->ResizeToDesignSize();

    return std::move(result);
}

HWND MainWindow::GetHWindow() const {
    return m_window;
}

HINSTANCE MainWindow::GetInstance() const {
    return m_instance;
}

void MainWindow::SetCommandHandler(CommandHandler handler) {
    m_commandHandler = std::move(handler);
}

void MainWindow::SetMessageHandler(MessageHandler handler) {
    m_messageHandler = std::move(handler);
}

void MainWindow::Show() {
    if (m_window == nullptr) {
        return;
    }

    ::ShowWindow(m_window, SW_SHOWNORMAL);
    ::SetForegroundWindow(m_window);
}

void MainWindow::Hide() {
    if (m_window == nullptr) {
        return;
    }

    ::ShowWindow(m_window, SW_HIDE);
}

void MainWindow::Toggle() {
    if (IsVisible()) {
        Hide();
    } else {
        Show();
    }
}

bool MainWindow::IsVisible() const {
    return m_window != nullptr && ::IsWindowVisible(m_window) != FALSE;
}

void MainWindow::SetMinimizeToTray(bool value) {
    m_minimizeToTray = value;
}

void MainWindow::SetHideOnClose(bool value) {
    m_hideOnClose = value;
}

void MainWindow::ApplyLanguage() {
    const std::wstring title = miniant::Lang::Wide(miniant::Lang::Str::WindowTitle);
    ::SetWindowTextW(m_window, title.c_str());

    for (const auto& button : m_buttons) {
        for (size_t i = 0; i < BUTTON_COUNT; ++i) {
            if (static_cast<UINT>(::GetDlgCtrlID(button.first)) == BUTTONS[i].id) {
                ::SetWindowTextW(button.first, miniant::Lang::Wide(BUTTONS[i].text).c_str());
                break;
            }
        }
    }

    if (m_fileMenu != nullptr) {
        // The two states carry a check mark: it is redrawn right away, because
        // ModifyMenu drops it.
        ::ModifyMenuW(
            m_fileMenu, 0, MF_BYPOSITION | MF_STRING | (m_latencyEnabled ? MF_CHECKED : MF_UNCHECKED),
            MENU_FILE_TOGGLE_ID, miniant::Lang::Wide(miniant::Lang::Str::TrayToggleEnabled).c_str());

        ::ModifyMenuW(
            m_fileMenu, 1, MF_BYPOSITION | MF_STRING | (m_startWithWindows ? MF_CHECKED : MF_UNCHECKED),
            MENU_FILE_START_WITH_WINDOWS_ID, miniant::Lang::Wide(miniant::Lang::Str::TrayStartWithWindows).c_str());

        ::ModifyMenuW(
            m_fileMenu, 3, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_FILE_EXIT_ID, miniant::Lang::Wide(miniant::Lang::Str::ButtonExit).c_str());
    }

    if (m_diagnosticsMenu != nullptr) {
        ::ModifyMenuW(
            m_diagnosticsMenu, 0, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_DIAGNOSTICS_REPORT_ID, miniant::Lang::Wide(miniant::Lang::Str::ButtonDiagnostics).c_str());

        ::ModifyMenuW(
            m_diagnosticsMenu, 1, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_DIAGNOSTICS_LOG_ID, miniant::Lang::Wide(miniant::Lang::Str::ButtonOpenLog).c_str());
    }

    if (m_menu != nullptr && m_fileMenu != nullptr && m_diagnosticsMenu != nullptr) {
        ::ModifyMenuW(
            m_menu,
            0,
            MF_BYPOSITION | MF_STRING | MF_POPUP,
            reinterpret_cast<UINT_PTR>(m_fileMenu),
            miniant::Lang::Wide(miniant::Lang::Str::ButtonFileMenu).c_str());

        // The menu bar: "File", "Options", "Diagnostics", "About".
        ::ModifyMenuW(
            m_menu,
            1,
            MF_BYPOSITION | MF_STRING,
            MENU_OPTIONS_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonOptions).c_str());

        ::ModifyMenuW(
            m_menu,
            2,
            MF_BYPOSITION | MF_STRING | MF_POPUP,
            reinterpret_cast<UINT_PTR>(m_diagnosticsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::ButtonDiagnostics).c_str());

        ::ModifyMenuW(
            m_menu,
            3,
            MF_BYPOSITION | MF_STRING,
            MENU_ABOUT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonAbout).c_str());
    }

    if (!m_statusTextSet && m_status != nullptr) {
        ::SetWindowTextW(m_status, miniant::Lang::Wide(miniant::Lang::Str::StatusStarting).c_str());
    }

    LayoutControls();
}

void MainWindow::SetMenuChecks(bool latencyEnabled, bool startWithWindows) {
    m_latencyEnabled = latencyEnabled;
    m_startWithWindows = startWithWindows;

    if (m_fileMenu != nullptr) {
        ::CheckMenuItem(m_fileMenu, MENU_FILE_TOGGLE_ID, MF_BYCOMMAND | (m_latencyEnabled ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(
            m_fileMenu, MENU_FILE_START_WITH_WINDOWS_ID,
            MF_BYCOMMAND | (m_startWithWindows ? MF_CHECKED : MF_UNCHECKED));
    }
}

void MainWindow::SetStatusText(const std::wstring& text) {
    if (m_status != nullptr) {
        ::SetWindowTextW(m_status, text.c_str());
        m_statusTextSet = true;
    }
}

void MainWindow::AppendLogLines(const std::vector<std::string>& lines) {
    if (m_log == nullptr || lines.empty()) {
        return;
    }

    const int currentLength = ::GetWindowTextLengthW(m_log);
    if (currentLength > static_cast<int>(MAX_LOG_LENGTH)) {
        ::SetWindowTextW(m_log, L"");
    }

    std::string text;
    for (const auto& line : lines) {
        text += line;
        text += "\r\n";
    }

    const std::wstring wide = Text::ToWide(text);
    const int length = ::GetWindowTextLengthW(m_log);
    ::SendMessageW(m_log, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    ::SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(wide.c_str()));

    // Always show the newest line, no matter where the caret was: put the caret
    // at the very end (without selecting anything) and scroll to the bottom.
    const int updatedLength = ::GetWindowTextLengthW(m_log);
    ::SendMessageW(m_log, EM_SETSEL, static_cast<WPARAM>(updatedLength), static_cast<LPARAM>(updatedLength));
    ::SendMessageW(m_log, EM_SCROLLCARET, 0, 0);
    ::SendMessageW(m_log, WM_VSCROLL, SB_BOTTOM, 0);
}

void MainWindow::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    if (m_tray) {
        m_tray->Notify(title, text, error);
    }
}

void MainWindow::SetTrayTooltip(const std::wstring& text) {
    if (m_tray) {
        m_tray->SetTooltip(text);
    }
}

void MainWindow::SetTrayMenuState(const TrayMenuState& state) {
    if (m_tray) {
        m_tray->SetMenuState(state);
    }
}

void MainWindow::EnableTray(bool enabled) {
    if (enabled == m_trayEnabled) {
        return;
    }

    m_trayEnabled = enabled;

    if (enabled) {
        const HICON icon = ::LoadIconW(m_instance, MAKEINTRESOURCEW(IDI_ICON1));
        m_tray = std::make_unique<TrayIcon>(m_window, WM_APP_TRAY, icon);
        m_tray->SetCommandHandler([this](miniant::Command command) {
            RaiseCommand(command);
            });

        if (!m_tray->Show()) {
            m_tray.reset();
            m_trayEnabled = false;
        }
    } else {
        m_tray.reset();
    }
}

bool MainWindow::IsTrayVisible() const {
    return m_tray != nullptr && m_tray->IsVisible();
}

void MainWindow::RaiseCommand(miniant::Command command) {
    if (m_commandHandler) {
        m_commandHandler(command);
    }
}

void MainWindow::CreateFonts() {
    if (m_uiFont != nullptr) {
        ::DeleteObject(m_uiFont);
        m_uiFont = nullptr;
    }

    if (m_monoFont != nullptr) {
        ::DeleteObject(m_monoFont);
        m_monoFont = nullptr;
    }

    m_uiFont = Dpi::CreateUiFont(m_dpi);
    m_monoFont = Dpi::CreateMonoFont(m_dpi);
}

// The fonts of every control are replaced: the controls themselves are not
// re-created, so the text the user has already seen in them stays.
void MainWindow::ApplyFonts() {
    if (m_uiFont != nullptr) {
        if (m_status != nullptr) {
            ::SendMessageW(m_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }

        for (auto& button : m_buttons) {
            ::SendMessageW(button.first, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }
    }

    if (m_monoFont != nullptr && m_log != nullptr) {
        ::SendMessageW(m_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont), TRUE);
    }
}

void MainWindow::ApplyDpi(UINT dpi) {
    m_dpi = dpi != 0 ? dpi : Dpi::ForSystem();

    miniant::Log::Debug(miniant::Lang::Utf8(miniant::Lang::Str::LogInterfaceScale), ::MulDiv(m_dpi, 100, 96));

    CreateFonts();
    ApplyFonts();
    LayoutControls();
}

// The client area the layout is written for, in the pixels of the monitor the
// window is on.
void MainWindow::ResizeToDesignSize() {
    if (m_window == nullptr) {
        return;
    }

    RECT desired = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    Dpi::AdjustWindowRect(desired, WS_OVERLAPPEDWINDOW, m_dpi);

    ::SetWindowPos(
        m_window, nullptr, 0, 0,
        desired.right - desired.left, desired.bottom - desired.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    LayoutControls();
}

int MainWindow::S(int value) const {
    return Dpi::Scale(value, m_dpi);
}

void MainWindow::CreateControls() {
    // The status line of the window: a sunken strip at the bottom, the way a
    // status bar of a program looks.
    m_status = ::CreateWindowExW(
        0,
        L"STATIC",
        miniant::Lang::Wide(miniant::Lang::Str::StatusStarting).c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_SUNKEN,
        0, 0, 0, 0,
        m_window,
        nullptr,
        m_instance,
        nullptr);

    m_log = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0,
        m_window,
        nullptr,
        m_instance,
        nullptr);

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        const HWND button = ::CreateWindowExW(
            0,
            L"BUTTON",
            miniant::Lang::Wide(BUTTONS[i].text).c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0,
            m_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(BUTTONS[i].id)),
            m_instance,
            nullptr);

        m_buttons.emplace_back(button, BUTTONS[i].command);
    }

    m_menu = ::CreateMenu();
    m_fileMenu = ::CreatePopupMenu();
    m_diagnosticsMenu = ::CreatePopupMenu();

    if (m_menu != nullptr && m_fileMenu != nullptr && m_diagnosticsMenu != nullptr) {
        // "File": the state of the latency reduction, the autostart and the exit.
        ::AppendMenuW(
            m_fileMenu, MF_STRING | MF_CHECKED, MENU_FILE_TOGGLE_ID,
            miniant::Lang::Wide(miniant::Lang::Str::TrayToggleEnabled).c_str());

        ::AppendMenuW(
            m_fileMenu, MF_STRING, MENU_FILE_START_WITH_WINDOWS_ID,
            miniant::Lang::Wide(miniant::Lang::Str::TrayStartWithWindows).c_str());

        ::AppendMenuW(m_fileMenu, MF_SEPARATOR, 0, nullptr);

        ::AppendMenuW(
            m_fileMenu, MF_STRING, MENU_FILE_EXIT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonExit).c_str());

        // "Diagnostics": the report and the log file.
        ::AppendMenuW(
            m_diagnosticsMenu, MF_STRING, MENU_DIAGNOSTICS_REPORT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonDiagnostics).c_str());

        ::AppendMenuW(
            m_diagnosticsMenu, MF_STRING, MENU_DIAGNOSTICS_LOG_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonOpenLog).c_str());

        ::AppendMenuW(
            m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_fileMenu),
            miniant::Lang::Wide(miniant::Lang::Str::ButtonFileMenu).c_str());

        ::AppendMenuW(
            m_menu, MF_STRING, MENU_OPTIONS_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonOptions).c_str());

        ::AppendMenuW(
            m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_diagnosticsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::ButtonDiagnostics).c_str());

        ::AppendMenuW(
            m_menu, MF_STRING, MENU_ABOUT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::ButtonAbout).c_str());

        ::SetMenu(m_window, m_menu);
    }

    ApplyFonts();
    LayoutControls();
}

void MainWindow::LayoutControls() {
    if (m_window == nullptr) {
        return;
    }

    RECT client = {};
    ::GetClientRect(m_window, &client);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    // The status line is the last strip of the window: the buttons sit above
    // it, the log fills everything that is left.
    const int statusTop = height - S(MARGIN) - S(STATUS_HEIGHT);
    ::MoveWindow(m_status, S(MARGIN), statusTop, width - 2 * S(MARGIN), S(STATUS_HEIGHT), TRUE);

    const int buttonsTop = statusTop - S(4) - S(BUTTON_HEIGHT);
    const int logTop = S(MARGIN);
    const int logHeight = std::max(S(40), buttonsTop - logTop - S(4));

    ::MoveWindow(m_log, S(MARGIN), logTop, width - 2 * S(MARGIN), logHeight, TRUE);

    // "Restart" is aligned to the left edge of the window, "Exit" to the right
    // one; the middle of the row stays empty.
    const int buttonWidth = std::min(S(BUTTON_WIDTH), std::max(S(80), (width - 3 * S(MARGIN)) / 2));

    for (size_t i = 0; i < m_buttons.size() && i < BUTTON_COUNT; ++i) {
        const bool left = BUTTONS[i].left;
        const int x = left ? S(MARGIN) : width - S(MARGIN) - buttonWidth;

        ::MoveWindow(m_buttons[i].first, x, buttonsTop, buttonWidth, S(BUTTON_HEIGHT), TRUE);
    }
}

void MainWindow::DestroyResources() {
    if (m_menu != nullptr) {
        ::DestroyMenu(m_menu);
        m_menu = nullptr;
        m_fileMenu = nullptr;
        m_diagnosticsMenu = nullptr;
    }

    if (m_uiFont != nullptr) {
        ::DeleteObject(m_uiFont);
        m_uiFont = nullptr;
    }

    if (m_monoFont != nullptr) {
        ::DeleteObject(m_monoFont);
        m_monoFont = nullptr;
    }
}

LRESULT CALLBACK MainWindow::WindowProcedureThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        if (self != nullptr) {
            self->m_window = window;
        }
    }

    auto* self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (self == nullptr) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    return self->WindowProcedure(message, wParam, lParam);
}

LRESULT MainWindow::WindowProcedure(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == m_taskbarCreatedMessage && m_taskbarCreatedMessage != 0) {
        if (m_tray) {
            m_tray->Recreate();
        }

        return 0;
    }

    switch (message) {
        case WM_DPICHANGED: {
            // The window is on another monitor (per-monitor DPI): the system
            // suggests the position and the size that match its new scale.
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                ::SetWindowPos(
                    m_window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }

            ApplyDpi(HIWORD(wParam));
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE && m_minimizeToTray) {
                Hide();
                return 0;
            }

            break;

        case WM_CLOSE:
            if (m_hideOnClose) {
                Hide();
                return 0;
            }

            RaiseCommand(miniant::Command::Exit);
            return 0;

        case WM_SIZE:
            LayoutControls();
            return 0;

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT notification = HIWORD(wParam);

            if (notification == BN_CLICKED) {
                for (const auto& button : m_buttons) {
                    if (::GetDlgCtrlID(button.first) == static_cast<int>(id)) {
                        RaiseCommand(button.second);
                        return 0;
                    }
                }
            }

            if (notification == 0) {
                if (id == MENU_OPTIONS_ID) {
                    RaiseCommand(miniant::Command::OpenSettings);
                    return 0;
                }

                if (id == MENU_ABOUT_ID) {
                    RaiseCommand(miniant::Command::About);
                    return 0;
                }

                switch (id) {
                    case MENU_FILE_TOGGLE_ID:
                        RaiseCommand(miniant::Command::ToggleEnabled);
                        return 0;

                    case MENU_FILE_START_WITH_WINDOWS_ID:
                        RaiseCommand(miniant::Command::ToggleStartWithWindows);
                        return 0;

                    case MENU_FILE_EXIT_ID:
                        RaiseCommand(miniant::Command::Exit);
                        return 0;

                    case MENU_DIAGNOSTICS_REPORT_ID:
                        RaiseCommand(miniant::Command::Diagnose);
                        return 0;

                    case MENU_DIAGNOSTICS_LOG_ID:
                        RaiseCommand(miniant::Command::OpenLog);
                        return 0;

                    default:
                        break;
                }
            }

            break;
        }

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (wParam != 0) {
                RaiseCommand(miniant::Command::Exit);
            }

            return 0;

        case WM_APP_LOG_LINES:
            AppendLogLines(Log::Buffer().TakePending());
            return 0;

        case WM_APP_TRAY:
            if (m_tray) {
                m_tray->HandleMessage(wParam, lParam);
            }

            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    if (m_messageHandler) {
        m_messageHandler(message, wParam, lParam);
    }

    return ::DefWindowProcW(m_window, message, wParam, lParam);
}
