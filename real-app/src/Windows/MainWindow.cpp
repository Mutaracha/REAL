#include "MainWindow.h"

#include "../../res/resource.h"
#include "../AppMessages.h"
#include "../Log.h"
#include "../Text.h"

#include <algorithm>

using namespace miniant;
using namespace miniant::Windows;

namespace {

const wchar_t WINDOW_CLASS_NAME[] = L"REAL.MainWindow";
const wchar_t WINDOW_TITLE[] = L"REAL - REduce Audio Latency";

const int WINDOW_WIDTH = 780;
const int WINDOW_HEIGHT = 480;

const int STATUS_HEIGHT = 26;
const int BUTTON_HEIGHT = 30;
const int MARGIN = 8;

const size_t MAX_LOG_LENGTH = 200000;

const struct {
    UINT id;
    const wchar_t* text;
    miniant::Command command;
} BUTTONS[] = {
    { 1, L"Reinitialize", miniant::Command::Reinitialize },
    { 2, L"Settings file...", miniant::Command::OpenSettings },
    { 3, L"Open log", miniant::Command::OpenLog },
    { 4, L"Check for updates", miniant::Command::CheckForUpdates },
    { 5, L"Hide to tray", miniant::Command::HideToTray },
    { 6, L"Exit", miniant::Command::Exit },
};

constexpr size_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

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
        return tl::make_unexpected(WindowsError("Could not register the main window class."));
    }

    RECT desired = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    ::AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);

    const HWND window = ::CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
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
        return tl::make_unexpected(WindowsError("Could not create the main window."));
    }

    result->m_window = window;
    result->CreateFonts();
    result->CreateControls();

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

void MainWindow::SetStatusText(const std::wstring& text) {
    if (m_status != nullptr) {
        ::SetWindowTextW(m_status, text.c_str());
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
    ::SendMessageW(m_log, EM_SCROLLCARET, 0, 0);
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
    m_uiFont = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_monoFont = ::CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
}

void MainWindow::CreateControls() {
    m_status = ::CreateWindowExW(
        0,
        L"STATIC",
        L"Starting...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
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
            BUTTONS[i].text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0,
            m_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(BUTTONS[i].id)),
            m_instance,
            nullptr);

        m_buttons.emplace_back(button, BUTTONS[i].command);
    }

    if (m_uiFont != nullptr) {
        ::SendMessageW(m_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        for (auto& button : m_buttons) {
            ::SendMessageW(button.first, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }
    }

    if (m_monoFont != nullptr) {
        ::SendMessageW(m_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont), TRUE);
    }

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

    ::MoveWindow(m_status, MARGIN, MARGIN, width - 2 * MARGIN, STATUS_HEIGHT, TRUE);

    const int buttonsTop = height - MARGIN - BUTTON_HEIGHT;
    const int logTop = MARGIN + STATUS_HEIGHT + 4;
    const int logHeight = std::max(40, buttonsTop - logTop - 4);

    ::MoveWindow(m_log, MARGIN, logTop, width - 2 * MARGIN, logHeight, TRUE);

    const int buttonCount = static_cast<int>(m_buttons.size());
    if (buttonCount > 0) {
        const int totalMargin = MARGIN * (buttonCount + 1);
        const int buttonWidth = std::max(60, (width - totalMargin) / buttonCount);

        int x = MARGIN;
        for (auto& button : m_buttons) {
            ::MoveWindow(button.first, x, buttonsTop, buttonWidth, BUTTON_HEIGHT, TRUE);
            x += buttonWidth + MARGIN;
        }
    }
}

void MainWindow::DestroyResources() {
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
