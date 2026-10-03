#include "SingleInstance.h"

using namespace miniant::Windows;

SingleInstance::~SingleInstance() {
    Release();
}

SingleInstance::State SingleInstance::Acquire(const wchar_t* name) {
    Release();

    // The mutex is created owned; when it exists already, it stays with the
    // copy that created it.
    m_mutex = ::CreateMutexW(nullptr, TRUE, name);
    if (m_mutex == nullptr) {
        m_error = ::GetLastError();
        return State::Unknown;
    }

    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        return State::AnotherRunning;
    }

    m_owned = true;
    return State::First;
}

bool SingleInstance::WaitForOwnership(DWORD timeoutMs) {
    if (m_mutex == nullptr) {
        return false;
    }

    if (m_owned) {
        return true;
    }

    // An abandoned mutex belonged to a copy that ended without giving it back:
    // it is owned now all the same.
    const DWORD result = ::WaitForSingleObject(m_mutex, timeoutMs);
    m_owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;

    return m_owned;
}

void SingleInstance::Release() {
    if (m_mutex != nullptr) {
        if (m_owned) {
            ::ReleaseMutex(m_mutex);
        }

        ::CloseHandle(m_mutex);
        m_mutex = nullptr;
    }

    m_owned = false;
}

DWORD SingleInstance::Error() const {
    return m_error;
}

SignalResult miniant::Windows::SignalRunningInstance(const wchar_t* windowClass, UINT message, UINT timeoutMs) {
    const HWND window = ::FindWindowW(windowClass, nullptr);
    if (window == nullptr) {
        return SignalResult::NoWindow;
    }

    if (message == 0) {
        return SignalResult::NotResponding;
    }

    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeoutW(
        window, message, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, timeoutMs, &result);

    return sent != 0 ? SignalResult::Delivered : SignalResult::NotResponding;
}
