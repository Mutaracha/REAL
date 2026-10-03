#pragma once

#include <Windows.h>

#include <string>

namespace miniant::Windows {

// A balloon that points at a control of a window, with a warning icon and a
// title: a tracking tooltip of the system. It disappears by itself after a
// while - the owner passes its WM_TIMER to OnTimer() - or when Hide() is
// called. The tooltip window is owned by the window and is destroyed with it;
// the owner calls Forget() when that happens.
class BalloonTip {
public:
    // The identifier of the timer on the owner window.
    explicit BalloonTip(UINT_PTR timerId);

    BalloonTip(const BalloonTip&) = delete;
    BalloonTip& operator=(const BalloonTip&) = delete;

    void Show(
        HWND owner,
        HWND control,
        const std::wstring& title,
        const std::wstring& text,
        int maxWidth,
        UINT durationMs);
    void Hide();
    // True when the timer was the one of the balloon.
    bool OnTimer(UINT_PTR timerId);
    void Forget();

private:
    UINT_PTR m_timerId;
    HWND m_owner = nullptr;
    HWND m_tip = nullptr;
};

}
