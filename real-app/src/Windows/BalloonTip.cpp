#include "BalloonTip.h"

#include <commctrl.h>

using namespace miniant::Windows;

namespace {

// The classic common controls of the program take the second version of the
// structure only.
TTTOOLINFOW ToolInfo(HWND owner, wchar_t* text) {
    TTTOOLINFOW info = {};
    info.cbSize = TTTOOLINFOW_V2_SIZE;
    info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    info.hwnd = owner;
    info.uId = 1;
    info.lpszText = text;
    return info;
}

}

BalloonTip::BalloonTip(UINT_PTR timerId):
    m_timerId(timerId) {}

void BalloonTip::Show(
    HWND owner,
    HWND control,
    const std::wstring& title,
    const std::wstring& text,
    int maxWidth,
    UINT durationMs) {
    if (owner == nullptr || control == nullptr) {
        return;
    }

    Hide();

    if (m_tip == nullptr || m_owner != owner) {
        m_owner = owner;
        m_tip = ::CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_BALLOON | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            owner,
            nullptr,
            reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(owner, GWLP_HINSTANCE)),
            nullptr);

        if (m_tip == nullptr) {
            return;
        }

        wchar_t empty[] = L"";
        TTTOOLINFOW info = ToolInfo(m_owner, empty);
        ::SendMessageW(m_tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }

    ::SendMessageW(m_tip, TTM_SETMAXTIPWIDTH, 0, maxWidth);

    std::wstring body = text;
    TTTOOLINFOW info = ToolInfo(m_owner, body.data());
    ::SendMessageW(m_tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    ::SendMessageW(
        m_tip, TTM_SETTITLEW, title.empty() ? TTI_NONE : TTI_WARNING, reinterpret_cast<LPARAM>(title.c_str()));

    // The stem points at the middle of the lower edge of the control.
    RECT rect = {};
    ::GetWindowRect(control, &rect);
    ::SendMessageW(
        m_tip, TTM_TRACKPOSITION, 0,
        MAKELPARAM(static_cast<WORD>((rect.left + rect.right) / 2), static_cast<WORD>(rect.bottom)));
    ::SendMessageW(m_tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&info));

    ::SetTimer(m_owner, m_timerId, durationMs, nullptr);
}

void BalloonTip::Hide() {
    if (m_tip != nullptr) {
        TTTOOLINFOW info = ToolInfo(m_owner, nullptr);
        ::SendMessageW(m_tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&info));
    }

    if (m_owner != nullptr) {
        ::KillTimer(m_owner, m_timerId);
    }
}

bool BalloonTip::OnTimer(UINT_PTR timerId) {
    if (timerId != m_timerId) {
        return false;
    }

    Hide();
    return true;
}

void BalloonTip::Forget() {
    m_tip = nullptr;
    m_owner = nullptr;
}
