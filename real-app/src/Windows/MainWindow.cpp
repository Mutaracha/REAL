#include "MainWindow.h"

#include "Dpi.h"

#include "../../res/resource.h"
#include "../AppMessages.h"
#include "../Lang.h"
#include "../Log.h"
#include "../Text.h"

#include <commctrl.h>

#include <algorithm>

using namespace miniant;
using namespace miniant::Windows;

namespace {

// The client area under the menu bar.
const int WINDOW_WIDTH = 680;
const int WINDOW_HEIGHT = 400;

const int STATUS_HEIGHT = 26;
const int MARGIN = 8;

// The log view keeps the newest part of the session: past this length the
// oldest half is dropped (see AppendLogLines).
const size_t MAX_LOG_LENGTH = 200000;

// The restart button sits at the right end of the status line: a circular
// arrow without a caption, the tooltip names it. It is square, as high as the
// status line, and the text of the status line ends a little before it.
const UINT RESTART_BUTTON_ID = 1;
const int STATUS_BUTTON_GAP = 2;
const int RESTART_GLYPH_HEIGHT = 14;

// "Refresh" of the icon font of Windows 10 and 11; a system without that font
// gets the clockwise open circle arrow of the symbol font.
const wchar_t ICON_FONT_FACE[] = L"Segoe MDL2 Assets";
const wchar_t RESTART_GLYPH[] = L"\xE72C";
const wchar_t FALLBACK_FONT_FACE[] = L"Segoe UI Symbol";
const wchar_t FALLBACK_RESTART_GLYPH[] = L"\x21BB";

// "REAL": the state of the program, the restart of the audio streams and the
// exit. The files are in "Diagnostics", the settings in "Options".
const UINT MENU_PROGRAM_TOGGLE_ID = 101;
const UINT MENU_PROGRAM_RESTART_ID = 102;
const UINT MENU_PROGRAM_EXIT_ID = 103;

// "Options": the settings window (the tray menu opens it by the same name) and
// the autostart.
const UINT MENU_OPTIONS_SETTINGS_ID = 104;
const UINT MENU_OPTIONS_START_WITH_WINDOWS_ID = 105;

// "Diagnostics": the report itself and the log file.
const UINT MENU_DIAGNOSTICS_REPORT_ID = 106;
const UINT MENU_DIAGNOSTICS_LOG_ID = 107;

// "About" is an item of the menu bar itself: the menu of a window can hold
// plain items next to its popups, and a click on one of them sends the very
// same command a drop-down entry would.
const UINT MENU_ABOUT_ID = 108;

int CALLBACK OnFontFamily(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM parameter) {
    *reinterpret_cast<bool*>(parameter) = true;
    return 0;
}

// Whether a font is installed: the icon font exists since Windows 10.
bool IsFontInstalled(const wchar_t* face) {
    const HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }

    LOGFONTW font = {};
    font.lfCharSet = DEFAULT_CHARSET;
    ::wcsncpy_s(font.lfFaceName, face, _TRUNCATE);

    bool found = false;
    ::EnumFontFamiliesExW(screen, &font, OnFontFamily, reinterpret_cast<LPARAM>(&found), 0);

    ::ReleaseDC(nullptr, screen);
    return found;
}

bool HasIconFont() {
    static const bool installed = IsFontInstalled(ICON_FONT_FACE);
    return installed;
}

// The tooltip of the restart button: the same text as the menu item.
void SetToolText(HWND tooltip, HWND owner, HWND tool, UINT message) {
    if (tooltip == nullptr || tool == nullptr) {
        return;
    }

    std::wstring text = miniant::Lang::Wide(miniant::Lang::Str::MenuRestart);

    // The size of the second version of the structure: the common controls of
    // the program are the classic ones, which do not take the larger one.
    TTTOOLINFOW info = {};
    info.cbSize = TTTOOLINFOW_V2_SIZE;
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = owner;
    info.uId = reinterpret_cast<UINT_PTR>(tool);
    info.lpszText = text.data();

    ::SendMessageW(tooltip, message, 0, reinterpret_cast<LPARAM>(&info));
}

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
    ::UnregisterClassW(MAIN_WINDOW_CLASS_NAME, m_instance);
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
    windowClass.lpszClassName = MAIN_WINDOW_CLASS_NAME;

    if (::RegisterClassExW(&windowClass) == 0) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::LogWindowClassFailed)));
    }

    // The window is created with the metrics of the desktop: the monitor it
    // lands on is only known when it exists (WM_DPICHANGED reports a move to
    // another monitor later, and the layout is scaled again).
    const UINT desktopDpi = Dpi::ForSystem();
    RECT desired = { 0, 0, Dpi::Scale(WINDOW_WIDTH, desktopDpi), Dpi::Scale(WINDOW_HEIGHT, desktopDpi) };
    Dpi::AdjustWindowRect(desired, WS_OVERLAPPEDWINDOW, desktopDpi, true);

    const std::wstring title = miniant::Lang::Wide(miniant::Lang::Str::WindowTitle);

    const HWND window = ::CreateWindowExW(
        0,
        MAIN_WINDOW_CLASS_NAME,
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

    if (m_programMenu != nullptr) {
        // The state carries a check mark: it is put back right away, because
        // ModifyMenu drops it.
        ::ModifyMenuW(
            m_programMenu, 0, MF_BYPOSITION | MF_STRING | (m_latencyEnabled ? MF_CHECKED : MF_UNCHECKED),
            MENU_PROGRAM_TOGGLE_ID, miniant::Lang::Wide(miniant::Lang::Str::TrayToggleEnabled).c_str());

        ::ModifyMenuW(
            m_programMenu, 1, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_PROGRAM_RESTART_ID, miniant::Lang::Wide(miniant::Lang::Str::MenuRestart).c_str());

        ::ModifyMenuW(
            m_programMenu, 3, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_PROGRAM_EXIT_ID, miniant::Lang::Wide(miniant::Lang::Str::MenuExit).c_str());
    }

    if (m_diagnosticsMenu != nullptr) {
        ::ModifyMenuW(
            m_diagnosticsMenu, 0, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_DIAGNOSTICS_REPORT_ID, miniant::Lang::Wide(miniant::Lang::Str::MenuDiagnostics).c_str());

        ::ModifyMenuW(
            m_diagnosticsMenu, 1, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_DIAGNOSTICS_LOG_ID, miniant::Lang::Wide(miniant::Lang::Str::MenuOpenLog).c_str());
    }

    if (m_optionsMenu != nullptr) {
        ::ModifyMenuW(
            m_optionsMenu, 0, MF_BYPOSITION | MF_STRING | MF_UNCHECKED,
            MENU_OPTIONS_SETTINGS_ID, miniant::Lang::Wide(miniant::Lang::Str::TraySettings).c_str());

        ::ModifyMenuW(
            m_optionsMenu, 1, MF_BYPOSITION | MF_STRING | (m_startWithWindows ? MF_CHECKED : MF_UNCHECKED),
            MENU_OPTIONS_START_WITH_WINDOWS_ID, miniant::Lang::Wide(miniant::Lang::Str::MenuStartWithWindows).c_str());
    }

    if (m_menu != nullptr && m_programMenu != nullptr && m_optionsMenu != nullptr && m_diagnosticsMenu != nullptr) {
        ::ModifyMenuW(
            m_menu,
            0,
            MF_BYPOSITION | MF_STRING | MF_POPUP,
            reinterpret_cast<UINT_PTR>(m_programMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuProgram).c_str());

        // The menu bar: "REAL", "Options", "Diagnostics", "About".
        ::ModifyMenuW(
            m_menu,
            1,
            MF_BYPOSITION | MF_STRING | MF_POPUP,
            reinterpret_cast<UINT_PTR>(m_optionsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuOptions).c_str());

        ::ModifyMenuW(
            m_menu,
            2,
            MF_BYPOSITION | MF_STRING | MF_POPUP,
            reinterpret_cast<UINT_PTR>(m_diagnosticsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuDiagnostics).c_str());

        ::ModifyMenuW(
            m_menu,
            3,
            MF_BYPOSITION | MF_STRING,
            MENU_ABOUT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuAbout).c_str());

        // The menu bar is drawn by the frame of the window: without this its
        // items kept the old texts until the mouse passed over each of them.
        ::DrawMenuBar(m_window);
    }

    SetToolText(m_tooltip, m_window, m_restartButton, TTM_UPDATETIPTEXTW);

    if (!m_statusTextSet && m_status != nullptr) {
        ::SetWindowTextW(m_status, miniant::Lang::Wide(miniant::Lang::Str::StatusStarting).c_str());
    }

    LayoutControls();
}

void MainWindow::SetMenuChecks(bool latencyEnabled, bool startWithWindows) {
    m_latencyEnabled = latencyEnabled;
    m_startWithWindows = startWithWindows;

    if (m_programMenu != nullptr) {
        ::CheckMenuItem(
            m_programMenu, MENU_PROGRAM_TOGGLE_ID, MF_BYCOMMAND | (m_latencyEnabled ? MF_CHECKED : MF_UNCHECKED));
    }

    if (m_optionsMenu != nullptr) {
        ::CheckMenuItem(
            m_optionsMenu, MENU_OPTIONS_START_WITH_WINDOWS_ID,
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

    // Past the limit the oldest lines go, up to a line break, so that the view
    // never starts in the middle of a line and the recent history stays.
    const int currentLength = ::GetWindowTextLengthW(m_log);
    if (currentLength > static_cast<int>(MAX_LOG_LENGTH)) {
        std::wstring current(static_cast<size_t>(currentLength) + 1, L'\0');
        ::GetWindowTextW(m_log, current.data(), currentLength + 1);

        const size_t keepFrom = static_cast<size_t>(currentLength) - MAX_LOG_LENGTH / 2;
        const size_t lineBreak = current.find(L"\r\n", keepFrom);

        if (lineBreak != std::wstring::npos) {
            ::SendMessageW(m_log, EM_SETSEL, 0, static_cast<LPARAM>(lineBreak + 2));
            ::SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        } else {
            ::SetWindowTextW(m_log, L"");
        }
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
    // A balloon is for the user who does not see the window: while it is open
    // (shown and not minimized) its journal already says the same.
    const bool windowOpen = IsVisible() && ::IsIconic(m_window) == FALSE;

    if (m_tray && !windowOpen) {
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
    m_uiFont = Dpi::CreateUiFont(m_dpi);
    m_monoFont = Dpi::CreateMonoFont(m_dpi);

    // The glyph of the restart button: the height of the character cell is
    // given, so the arrow keeps its size whatever the font of the system is.
    m_iconFont = ::CreateFontW(
        -S(RESTART_GLYPH_HEIGHT), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        HasIconFont() ? ICON_FONT_FACE : FALLBACK_FONT_FACE);
}

// The fonts of every control are replaced: the controls themselves are not
// re-created, so the text the user has already seen in them stays.
void MainWindow::ApplyFonts() {
    if (m_uiFont != nullptr) {
        if (m_status != nullptr) {
            ::SendMessageW(m_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }
    }

    if (m_iconFont != nullptr && m_restartButton != nullptr) {
        ::SendMessageW(m_restartButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_iconFont), TRUE);
    }

    if (m_monoFont != nullptr && m_log != nullptr) {
        ::SendMessageW(m_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont), TRUE);
    }
}

void MainWindow::ApplyDpi(UINT dpi) {
    m_dpi = dpi != 0 ? dpi : Dpi::ForSystem();

    miniant::Log::Debug(miniant::Lang::Utf8(miniant::Lang::Str::LogInterfaceScale), ::MulDiv(m_dpi, 100, 96));

    // A control keeps drawing with the font it was given: the old fonts are
    // deleted only when every control has got its new one.
    const HFONT oldFonts[] = { m_uiFont, m_monoFont, m_iconFont };

    CreateFonts();
    ApplyFonts();

    for (const HFONT font : oldFonts) {
        if (font != nullptr) {
            ::DeleteObject(font);
        }
    }

    LayoutControls();
}

// The client area the layout is written for, in the pixels of the monitor the
// window is on.
void MainWindow::ResizeToDesignSize() {
    if (m_window == nullptr) {
        return;
    }

    RECT desired = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    Dpi::AdjustWindowRect(desired, WS_OVERLAPPEDWINDOW, m_dpi, true);

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

    // An edit control accepts about 32 000 characters by default, and
    // EM_REPLACESEL silently stops appending at that limit: the view would
    // freeze after a few hundred lines. The limit is lifted, the length is
    // kept in check by AppendLogLines instead.
    if (m_log != nullptr) {
        ::SendMessageW(m_log, EM_SETLIMITTEXT, 0, 0);
    }

    // The restart button of the status line. Its caption is the glyph of a
    // circular arrow; the name is in the tooltip.
    m_restartButton = ::CreateWindowExW(
        0,
        L"BUTTON",
        HasIconFont() ? RESTART_GLYPH : FALLBACK_RESTART_GLYPH,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
        0, 0, 0, 0,
        m_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(RESTART_BUTTON_ID)),
        m_instance,
        nullptr);

    INITCOMMONCONTROLSEX classes = {};
    classes.dwSize = sizeof(classes);
    classes.dwICC = ICC_TAB_CLASSES;
    ::InitCommonControlsEx(&classes);

    m_tooltip = ::CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_window,
        nullptr,
        m_instance,
        nullptr);

    SetToolText(m_tooltip, m_window, m_restartButton, TTM_ADDTOOLW);

    m_menu = ::CreateMenu();
    m_programMenu = ::CreatePopupMenu();
    m_optionsMenu = ::CreatePopupMenu();
    m_diagnosticsMenu = ::CreatePopupMenu();

    if (m_menu != nullptr && m_programMenu != nullptr && m_optionsMenu != nullptr && m_diagnosticsMenu != nullptr) {
        // "REAL": the state of the latency reduction, the restart and the exit.
        ::AppendMenuW(
            m_programMenu, MF_STRING | MF_CHECKED, MENU_PROGRAM_TOGGLE_ID,
            miniant::Lang::Wide(miniant::Lang::Str::TrayToggleEnabled).c_str());

        ::AppendMenuW(
            m_programMenu, MF_STRING, MENU_PROGRAM_RESTART_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuRestart).c_str());

        ::AppendMenuW(m_programMenu, MF_SEPARATOR, 0, nullptr);

        ::AppendMenuW(
            m_programMenu, MF_STRING, MENU_PROGRAM_EXIT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuExit).c_str());

        // "Options": the settings window and the autostart.
        ::AppendMenuW(
            m_optionsMenu, MF_STRING, MENU_OPTIONS_SETTINGS_ID,
            miniant::Lang::Wide(miniant::Lang::Str::TraySettings).c_str());

        ::AppendMenuW(
            m_optionsMenu, MF_STRING, MENU_OPTIONS_START_WITH_WINDOWS_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuStartWithWindows).c_str());

        // "Diagnostics": the report and the log file.
        ::AppendMenuW(
            m_diagnosticsMenu, MF_STRING, MENU_DIAGNOSTICS_REPORT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuDiagnostics).c_str());

        ::AppendMenuW(
            m_diagnosticsMenu, MF_STRING, MENU_DIAGNOSTICS_LOG_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuOpenLog).c_str());

        ::AppendMenuW(
            m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_programMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuProgram).c_str());

        ::AppendMenuW(
            m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_optionsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuOptions).c_str());

        ::AppendMenuW(
            m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_diagnosticsMenu),
            miniant::Lang::Wide(miniant::Lang::Str::MenuDiagnostics).c_str());

        ::AppendMenuW(
            m_menu, MF_STRING, MENU_ABOUT_ID,
            miniant::Lang::Wide(miniant::Lang::Str::MenuAbout).c_str());

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

    // The status line is the last strip of the window, the restart button ends
    // it on the right; the log fills everything above.
    const int statusTop = height - S(MARGIN) - S(STATUS_HEIGHT);
    const int buttonSize = S(STATUS_HEIGHT);
    const int buttonLeft = width - S(MARGIN) - buttonSize;
    const int statusWidth = std::max(0, buttonLeft - S(STATUS_BUTTON_GAP) - S(MARGIN));

    ::MoveWindow(m_status, S(MARGIN), statusTop, statusWidth, S(STATUS_HEIGHT), TRUE);

    if (m_restartButton != nullptr) {
        ::MoveWindow(m_restartButton, buttonLeft, statusTop, buttonSize, buttonSize, TRUE);
    }

    const int logTop = S(MARGIN);
    const int logHeight = std::max(S(40), statusTop - S(4) - logTop);

    ::MoveWindow(m_log, S(MARGIN), logTop, width - 2 * S(MARGIN), logHeight, TRUE);
}

void MainWindow::DestroyResources() {
    if (m_menu != nullptr) {
        ::DestroyMenu(m_menu);
        m_menu = nullptr;
        m_programMenu = nullptr;
        m_optionsMenu = nullptr;
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

    if (m_iconFont != nullptr) {
        ::DeleteObject(m_iconFont);
        m_iconFont = nullptr;
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

            // The restart button: a control sends its handle along, a menu does not.
            if (notification == BN_CLICKED && lParam != 0 && id == RESTART_BUTTON_ID) {
                RaiseCommand(miniant::Command::Reinitialize);
                return 0;
            }

            if (notification == 0) {
                if (id == MENU_OPTIONS_SETTINGS_ID) {
                    RaiseCommand(miniant::Command::OpenSettings);
                    return 0;
                }

                if (id == MENU_ABOUT_ID) {
                    RaiseCommand(miniant::Command::About);
                    return 0;
                }

                switch (id) {
                    case MENU_PROGRAM_TOGGLE_ID:
                        RaiseCommand(miniant::Command::ToggleEnabled);
                        return 0;

                    case MENU_PROGRAM_RESTART_ID:
                        RaiseCommand(miniant::Command::Reinitialize);
                        return 0;

                    case MENU_PROGRAM_EXIT_ID:
                        RaiseCommand(miniant::Command::Exit);
                        return 0;

                    case MENU_OPTIONS_START_WITH_WINDOWS_ID:
                        RaiseCommand(miniant::Command::ToggleStartWithWindows);
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
            // The menu bar and its menus go with the window: their handles must
            // not be destroyed a second time (see DestroyResources).
            if (m_menu != nullptr && ::GetMenu(m_window) == m_menu) {
                m_menu = nullptr;
                m_programMenu = nullptr;
                m_optionsMenu = nullptr;
                m_diagnosticsMenu = nullptr;
            }

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
