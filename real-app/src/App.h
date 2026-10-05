#pragma once

#include "AudioSession.h"
#include "CommandLine.h"
#include "Commands.h"
#include "Http/HttpClient.h"
#include "Lang.h"
#include "Settings.h"
#include "Windows/MainWindow.h"
#include "Windows/SettingsWindow.h"
#include "Windows/SingleInstance.h"

#include <Windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace miniant {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();

private:
    // A message about the settings file that was collected before the log
    // exists: the file is read before the log is created, so the lines are
    // written into the window afterwards (see LogStartupMessages).
    enum class StartupLevel {
        Info,
        Warn,
        Error,
    };

    enum class TimerId : UINT_PTR {
        Validate = 1,
        DeviceEvent = 2,
        AudioRetry = 3,
    };

    bool LoadSettings();
    // The options of the command line (--tray, --no-tray, --log-level)
    // hold for this run only: they are laid over the settings of the file.
    void ApplyCommandLine(Config::Settings& settings) const;
    void LogStartupMessages();
    bool InitializeLogging();
    bool InitializeUi();
    void InitializeAudio();
    void InitializeTray();
    // Minimize and the close button hide the window in the tray only while
    // the tray icon is on screen.
    void ApplyWindowBehaviour();
    void RegisterSignalMessages();
    // One copy of the program runs at a time: a second start passes its
    // command to the running copy and ends. True when this process has to end
    // (the exit code is in exitCode). A copy that is just ending is waited
    // for, and this start becomes the running copy then.
    bool HandOverToRunningInstance(int& exitCode);
    void ReportInstanceNotResponding() const;
    void ApplyPerformanceSettings();
    void ApplyAudio();
    // Switches the mode on or off the same way for every caller: the menus and
    // the command of a second copy of the program.
    void SetAudioEnabled(bool enabled);
    void UpdateStatus();
    void UpdateTrayMenuState();
    void SaveSettings();
    // Writes m_fileSettings; a file that could not be parsed at the start is
    // kept next to it first (".bad"), so a typo never costs the whole file.
    bool WriteSettingsFile();
    // The window edits a copy of the settings; a saved copy is written to the
    // file and applied exactly like a file that was changed by hand.
    void ShowSettingsDialog();
    void ApplySettings(const Config::Settings& previous);
    // Tells the user why the log cannot be opened right now.
    void ReportLogUnavailable(Lang::Str reason);
    void OpenLogFile();
    void ShowAboutDialog();
    void StartUpdateCheck();
    void FinishUpdateCheck();
    // Interrupts a check that is waiting for the network and waits for its
    // thread, which then ends at once.
    void CancelUpdateCheck();
    void OnCommand(Command command);
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnTimer(UINT_PTR timerId);
    void OnDeviceEvent(LPARAM lParam);
    void OnSystemResume(bool sessionUnlock, const std::wstring& reason);
    void ScheduleDeviceRestart();
    void ScheduleAudioRetry();
    void CancelAudioRetry();
    void GiveUpOnDevice();

    // Rewrites the settings file when the comments are written in another
    // language (the values are kept).
    void RefreshCommentsLanguage();

    // Language of the comments after the settings file was rewritten.
    std::vector<std::pair<StartupLevel, std::string>> m_startupMessages;
    std::string m_commentsRewritten;
    // The file was rewritten because its layout was older than the current one.
    bool m_layoutUpgraded = false;
    // The file exists but could not be parsed: it is backed up before the
    // first write (see WriteSettingsFile).
    bool m_settingsBroken = false;

    // Text shown while the latency reduction is not applied.
    std::wstring CurrentOffStatusText() const;
    int RunDiagnostics();
    void ShowDiagnostics();
    std::wstring WriteDiagnosticsReport(const std::string& report);
    // The command line the autostart entry has to contain.
    std::wstring AutostartCommand() const;
    // Writes the entry (creating the key when needed) and reports a failure.
    bool WriteAutostartCommand();
    void SetStartWithWindows(bool enabled);
    // The registry entry follows application.startWithWindows: the value is
    // authoritative, the menu item and the autostart are only its reflection.
    void ApplyStartWithWindows();
    void PrintStartupText(const std::wstring& text) const;
    void CleanupPreviousInstall();
    void LogBanner();
    // The part of the shutdown that has to be done while the window exists:
    // the update check, the timers, the streams and the last line of the
    // journal. The Exit command runs it before the window is destroyed, so it
    // is done even when Windows ends the process right after the end of the
    // session (WM_ENDSESSION).
    void StopWork();
    void Shutdown();

    HINSTANCE m_instance = nullptr;

    CommandLine::Options m_options;
    // The settings in effect: the file plus the options of the command line.
    Config::Settings m_settings;
    // The settings as they are in the file: this is what the options window
    // edits and what is written back, so an option of one run never ends up
    // in the file.
    Config::Settings m_fileSettings;
    std::wstring m_settingsPath;

    std::unique_ptr<Windows::MainWindow> m_window;
    Audio::AudioSession m_audio;

    bool m_audioEnabled = true;
    bool m_comInitialized = false;
    bool m_workStopped = false;
    bool m_shuttingDown = false;
    int m_exitCode = 0;

    Windows::SingleInstance m_singleInstance;
    // The settings window is modal: a second one is never opened.
    bool m_settingsWindowOpen = false;

    UINT m_signalShow = 0;
    UINT m_signalReinitialize = 0;
    UINT m_signalEnable = 0;
    UINT m_signalDisable = 0;
    UINT m_signalExit = 0;

    std::thread m_updateThread;
    // Stops the request of the check when the program is closing; the thread
    // of the check holds it while the request runs.
    std::shared_ptr<Http::Cancellation> m_updateCancellation;
    std::mutex m_updateMutex;
    std::string m_updateMessage;
    std::string m_updateDetails;
    std::string m_updateUrl;

    // Backoff for re-applying the audio after a transient failure (device being
    // enabled/disabled, audio service restarting).
    unsigned int m_retryDelayMs = 0;
    // Tick count when the current outage started (0 = everything is fine).
    ULONGLONG m_failureSince = 0;
    // Set when the application gave up after audio.reinit.failureTimeoutSec and
    // stopped polling until a device event arrives.
    bool m_audioSuspended = false;
    // The next apply is caused by a device change (affects the notification).
    bool m_deviceChangePending = false;
    // The start of the current outage was shown by a balloon: its end gets one
    // as well (see ApplyAudio).
    bool m_failureNotified = false;
    // The timer of the device events was started by a device event, not only
    // by the end of a sleep or a lock (see OnTimer).
    bool m_deviceEventPending = false;
    // Why the streams are created again after a sleep or a lock, for the line
    // of the journal.
    std::wstring m_resumeReason;
    // The next apply follows a switch the user asked for (the menus, a
    // command, a restart): it is the one that may show a balloon about it.
    bool m_stateChangePending = false;
    // The buffer range of the default device, from the last apply: the settings
    // window shows it next to the fixed buffer and checks the value with it.
    Windows::BufferRange m_bufferRange;
};

}
