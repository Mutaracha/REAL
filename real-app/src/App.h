#pragma once

#include "AudioSession.h"
#include "CommandLine.h"
#include "Commands.h"
#include "Lang.h"
#include "Settings.h"
#include "Windows/MainWindow.h"

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
    // The options of the command line (--tray, --multi-instance, --log-level)
    // hold for this run only: they are laid over the settings of the file.
    void ApplyCommandLine(Config::Settings& settings) const;
    void LogStartupMessages();
    bool InitializeLogging();
    bool InitializeUi();
    void InitializeAudio();
    void InitializeTray();
    void RegisterSignalMessages();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void ApplyPerformanceSettings();
    void ApplyAudio();
    // Switches the mode on or off the same way for every caller: the tray item,
    // the hotkey and the command of a second copy of the program.
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
    // The settings that a running program cannot apply (a single copy of the
    // program is one of them) ask for a restart: the user is asked about it and
    // the program starts itself again with the same command line.
    bool NeedsRestart(const Config::Settings& previous) const;
    void AskForRestart();
    void RestartApplication();
    void ShowAboutDialog();
    void StartUpdateCheck();
    void FinishUpdateCheck();
    void OnCommand(Command command);
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnTimer(UINT_PTR timerId);
    void OnDeviceEvent(WPARAM wParam, LPARAM lParam);
    void OnSystemResume(bool sessionUnlock, const std::wstring& reason);
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
    // The command line of the autostart entry, empty when there is none.
    std::wstring ReadAutostartCommand() const;
    std::wstring AutostartCommand() const;
    // Writes the entry (creating the key when needed) and reports a failure.
    bool WriteAutostartCommand();
    void SetStartWithWindows(bool enabled);
    // The registry entry follows application.startWithWindows: the value is
    // authoritative, the menu item and the autostart are only its reflection.
    void ApplyStartWithWindows();
    bool NotifyRunningInstance(UINT message) const;
    void PrintStartupText(const std::wstring& text) const;
    void CleanupPreviousInstall();
    void LogBanner();
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
    bool m_shuttingDown = false;
    int m_exitCode = 0;

    HANDLE m_instanceMutex = nullptr;
    bool m_anotherInstanceRuns = false;
    // The settings window is modal: a second one is never opened.
    bool m_settingsWindowOpen = false;

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
    // Tick count when the current outage started (0 = everything is fine).
    ULONGLONG m_failureSince = 0;
    // Set when the application gave up after audio.reinit.failureTimeoutMs and
    // stopped polling until a device event arrives.
    bool m_audioSuspended = false;
    // The next apply is caused by a device change (affects the notification).
    bool m_deviceChangePending = false;
};

}
