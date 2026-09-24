#pragma once

#include "AudioSession.h"
#include "CommandLine.h"
#include "Commands.h"
#include "Settings.h"
#include "Windows/MainWindow.h"

#include <Windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace miniant {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();

private:
    enum class TimerId : UINT_PTR {
        Validate = 1,
        DeviceEvent = 2,
        AudioRetry = 3,
    };

    bool LoadSettings();
    bool InitializeLogging();
    bool InitializeUi();
    void InitializeAudio();
    void InitializeTray();
    void RegisterSignalMessages();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void ApplyPerformanceSettings();
    void ApplyAudio();
    void UpdateStatus();
    void UpdateTrayMenuState();
    void SaveSettings();
    void ReloadSettings();
    void OpenSettingsFile();
    void OpenLogFile();
    void ShowAboutDialog();
    void StartUpdateCheck();
    void FinishUpdateCheck();
    void OnCommand(Command command);
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnTimer(UINT_PTR timerId);
    void OnDeviceEvent(WPARAM wParam, LPARAM lParam);
    void OnSystemResume(const std::wstring& reason);
    void ScheduleAudioRetry();
    void CancelAudioRetry();
    void GiveUpOnDevice();

    // Text shown while the latency reduction is not applied.
    std::wstring CurrentOffStatusText() const;
    int RunDiagnostics();
    void ShowDiagnostics();
    std::wstring WriteDiagnosticsReport(const std::string& report);
    bool IsStartWithWindowsEnabled() const;
    void SetStartWithWindows(bool enabled);
    bool NotifyRunningInstance(UINT message) const;
    void PrintStartupText(const std::wstring& text) const;
    void CleanupPreviousInstall();
    void LogBanner();
    void Shutdown();

    HINSTANCE m_instance = nullptr;

    CommandLine::Options m_options;
    Config::Settings m_settings;
    std::wstring m_settingsPath;

    std::unique_ptr<Windows::MainWindow> m_window;
    Audio::AudioSession m_audio;

    bool m_audioEnabled = true;
    bool m_comInitialized = false;
    bool m_shuttingDown = false;
    int m_exitCode = 0;

    HANDLE m_instanceMutex = nullptr;
    bool m_anotherInstanceRuns = false;

    UINT m_signalShow = 0;
    UINT m_signalReinitialize = 0;
    UINT m_signalEnable = 0;
    UINT m_signalDisable = 0;
    UINT m_signalExit = 0;

    std::thread m_updateThread;
    std::mutex m_updateMutex;
    std::string m_updateMessage;
    std::string m_updateDetails;
    std::string m_updateUrl;

    // Backoff for re-applying the audio after a transient failure (device being
    // enabled/disabled, audio service restarting).
    unsigned int m_retryDelayMs = 0;
    bool m_lastApplyFailed = false;
    // Tick count when the current outage started (0 = everything is fine).
    ULONGLONG m_failureSince = 0;
    // Set when the application gave up after audio.reinit.failureTimeoutMs and
    // stopped polling until a device event arrives.
    bool m_audioSuspended = false;
    // The next apply is caused by a device change (affects the notification).
    bool m_deviceChangePending = false;
};

}
