#include "TrayIcon.h"

#include "../Lang.h"
#include "../Text.h"

#include <cwchar>

using namespace miniant::Windows;
using namespace miniant::Lang;

namespace {

enum MenuId : UINT {
    MENU_ID_TOGGLE = 1001,
    MENU_ID_REINITIALIZE = 1002,
    MENU_ID_SETTINGS = 1003,
    MENU_ID_LOG = 1004,
    MENU_ID_DIAGNOSTICS = 1005,
    MENU_ID_START_WITH_WINDOWS = 1006,
    MENU_ID_ABOUT = 1007,
    MENU_ID_EXIT = 1008,
};

constexpr size_t TOOLTIP_MAX_LENGTH = 120;

// A balloon cannot show more than 256 characters, and the shell cuts the text
// long before that; keep the notifications short.
constexpr size_t NOTIFICATION_TITLE_MAX_LENGTH = 48;
constexpr size_t NOTIFICATION_TEXT_MAX_LENGTH = 120;

// Two clicks arrive as two messages (a legacy one and NIN_SELECT), and a double
// click arrives as two selections: ignore everything that follows too quickly.
constexpr ULONGLONG TOGGLE_DEBOUNCE_MS = 350;

std::wstring Truncate(const std::wstring& text, size_t limit) {
    if (text.size() <= limit) {
        return text;
    }

    return text.substr(0, limit - 3) + L"...";
}

}

TrayIcon::TrayIcon(HWND owner, UINT callbackMessage, HICON icon):
    m_owner(owner) {
    m_data = {};
    m_data.cbSize = sizeof(m_data);
    m_data.hWnd = m_owner;
    m_data.uID = 1;
    m_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_data.uCallbackMessage = callbackMessage;
    m_data.hIcon = icon;
}

TrayIcon::~TrayIcon() {
    Hide();
}

tl::expected<void, WindowsError> TrayIcon::Show() {
    if (m_visible) {
        return {};
    }

    if (::Shell_NotifyIconW(NIM_ADD, &m_data) == FALSE) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::LogTrayCreateFailed)));
    }

    NOTIFYICONDATAW versionData = m_data;
    versionData.uVersion = NOTIFYICON_VERSION_4;
    if (::Shell_NotifyIconW(NIM_SETVERSION, &versionData) == FALSE) {
        ::Shell_NotifyIconW(NIM_DELETE, &m_data);
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::LogTrayConfigureFailed)));
    }

    m_visible = true;
    return {};
}

void TrayIcon::Hide() {
    if (!m_visible) {
        return;
    }

    ::Shell_NotifyIconW(NIM_DELETE, &m_data);
    m_visible = false;
}

bool TrayIcon::IsVisible() const {
    return m_visible;
}

void TrayIcon::SetTooltip(const std::wstring& text) {
    const std::wstring truncated = Truncate(text, TOOLTIP_MAX_LENGTH);
    wcscpy_s(m_data.szTip, truncated.c_str());

    if (m_visible) {
        m_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        ::Shell_NotifyIconW(NIM_MODIFY, &m_data);
    }
}

void TrayIcon::SetMenuState(const TrayMenuState& state) {
    m_state = state;
}

void TrayIcon::SetCommandHandler(CommandHandler handler) {
    m_handler = std::move(handler);
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, bool error) {
    if (!m_visible) {
        return;
    }

    NOTIFYICONDATAW data = m_data;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    wcscpy_s(data.szInfoTitle, Truncate(title, NOTIFICATION_TITLE_MAX_LENGTH).c_str());
    wcscpy_s(data.szInfo, Truncate(text, NOTIFICATION_TEXT_MAX_LENGTH).c_str());

    ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayIcon::Recreate() {
    const bool wasVisible = m_visible;
    m_visible = false;

    if (wasVisible) {
        Show();
    }
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    // The icon is registered with NOTIFYICON_VERSION_4, so the callback receives
    // the icon id and the event in the low words of wParam and lParam. The high
    // words carry the mouse message and the cursor position, which must not be
    // mistaken for an id or an event: doing so made single clicks work only
    // every other time.
    const UINT event = LOWORD(lParam);
    const UINT iconId = LOWORD(wParam);

    if (iconId != 0 && iconId != m_data.uID) {
        return;
    }

    switch (event) {
        case NIN_SELECT:
        case NIN_KEYSELECT: {
            const ULONGLONG now = ::GetTickCount64();
            if (now - m_lastToggleTick < TOGGLE_DEBOUNCE_MS) {
                // A double click or a duplicated message: ignore.
                break;
            }

            m_lastToggleTick = now;

            if (m_handler) {
                m_handler(miniant::Command::ToggleWindow);
            }

            break;
        }

        case WM_CONTEXTMENU:
            ShowContextMenu();
            break;

        case NIN_BALLOONUSERCLICK:
            if (m_handler) {
                m_handler(miniant::Command::BalloonClicked);
            }

            break;

        default:
            // NIN_BALLOONSHOW/HIDE/TIMEOUT and the legacy mouse messages that
            // accompany a version 4 notification carry no action.
            break;
    }
}

void TrayIcon::ShowContextMenu() {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    if (m_state.showStatus) {
        ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, Truncate(m_state.statusText, 90).c_str());
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    if (m_state.toggleEnabled) {
        ::AppendMenuW(menu, MF_STRING | (m_state.enabled ? MF_CHECKED : 0), MENU_ID_TOGGLE, Wide(Str::TrayToggleEnabled).c_str());
    }

    if (m_state.reinitialize) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_REINITIALIZE, Wide(Str::TrayReinitialize).c_str());
    }

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (m_state.openSettings) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_SETTINGS, Wide(Str::TraySettings).c_str());
    }

    if (m_state.openLog) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_LOG, Wide(Str::TrayLog).c_str());
    }

    if (m_state.diagnostics) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_DIAGNOSTICS, Wide(Str::TrayDiagnostics).c_str());
    }

    if (m_state.startWithWindows) {
        ::AppendMenuW(
            menu,
            MF_STRING | (m_state.startWithWindowsChecked ? MF_CHECKED : 0),
            MENU_ID_START_WITH_WINDOWS,
            Wide(Str::TrayStartWithWindows).c_str());
    }

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (m_state.about) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_ABOUT, Wide(Str::TrayAbout).c_str());
    }

    if (m_state.exit) {
        ::AppendMenuW(menu, MF_STRING, MENU_ID_EXIT, Wide(Str::TrayExit).c_str());
    }

    POINT cursor = {};
    ::GetCursorPos(&cursor);

    ::SetForegroundWindow(m_owner);

    const UINT selected = static_cast<UINT>(::TrackPopupMenuEx(
        menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        m_owner,
        nullptr));

    ::DestroyMenu(menu);

    ::PostMessageW(m_owner, WM_NULL, 0, 0);

    if (selected == 0 || !m_handler) {
        return;
    }

    switch (selected) {
        case MENU_ID_TOGGLE:
            m_handler(miniant::Command::ToggleEnabled);
            break;
        case MENU_ID_REINITIALIZE:
            m_handler(miniant::Command::Reinitialize);
            break;
        case MENU_ID_SETTINGS:
            m_handler(miniant::Command::OpenSettings);
            break;
        case MENU_ID_LOG:
            m_handler(miniant::Command::OpenLog);
            break;
        case MENU_ID_DIAGNOSTICS:
            m_handler(miniant::Command::Diagnose);
            break;
        case MENU_ID_START_WITH_WINDOWS:
            m_handler(miniant::Command::ToggleStartWithWindows);
            break;
        case MENU_ID_ABOUT:
            m_handler(miniant::Command::About);
            break;
        case MENU_ID_EXIT:
            m_handler(miniant::Command::Exit);
            break;
        default:
            break;
    }
}
