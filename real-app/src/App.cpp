#include "App.h"

#include <wtsapi32.h>

#include <cwchar>

#include "AppMessages.h"
#include "AppVersion.h"
#include "AutoUpdater.h"
#include "Lang.h"
#include "Log.h"
#include "Text.h"
#include "Windows/AboutWindow.h"
#include "Windows/Autostart.h"
#include "Windows/Diagnostics.h"
#include "Windows/Filesystem.h"
#include "Windows/SettingsWindow.h"
#include "Windows/SingleInstance.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace miniant;
using namespace miniant::Lang;

namespace {

// The first retry happens quickly, later ones slow down; the endpoint usually
// becomes available again as soon as the device has been switched on.
constexpr unsigned int INITIAL_AUDIO_RETRY_MS = 2000;
constexpr unsigned int MAXIMUM_AUDIO_RETRY_MS = 30000;

// audio.reinit.failureTimeoutSec in milliseconds: after this long without an
// answer the mode is switched off and the application stops polling the
// device. A value that is not set falls back to the default of the file.
unsigned int FailureTimeoutMs(const Config::ReinitSettings& reinit) {
    const int seconds = reinit.failureTimeoutSec > 0
        ? reinit.failureTimeoutSec
        : Config::ReinitSettings().failureTimeoutSec;
    return static_cast<unsigned int>(seconds) * 1000u;
}

// audio.reinit.debounceSec in milliseconds.
unsigned int DebounceMs(const Config::ReinitSettings& reinit) {
    const int seconds = reinit.debounceSec > 0 ? reinit.debounceSec : Config::ReinitSettings().debounceSec;
    return static_cast<unsigned int>(seconds) * 1000u;
}

constexpr const wchar_t* DIAGNOSTICS_FILE_NAME = L"REAL-diagnostics.txt";

const wchar_t INSTANCE_MUTEX_NAME[] = L"Local\\REAL.SingleInstance";

// How long the running copy may take to process a command of a second start,
// and how long a second start waits for a copy that is just ending (or just
// starting and has no window yet), asking every POLL_MS.
constexpr UINT SIGNAL_TIMEOUT_MS = 3000;
constexpr ULONGLONG HAND_OVER_WAIT_MS = 10000;
constexpr DWORD HAND_OVER_POLL_MS = 200;

const wchar_t SIGNAL_SHOW[] = L"REAL.Signal.Show";
const wchar_t SIGNAL_REINITIALIZE[] = L"REAL.Signal.Reinitialize";
const wchar_t SIGNAL_ENABLE[] = L"REAL.Signal.Enable";
const wchar_t SIGNAL_DISABLE[] = L"REAL.Signal.Disable";
const wchar_t SIGNAL_EXIT[] = L"REAL.Signal.Exit";

const UINT VALIDATE_INTERVAL_MS = 30000;

// Writes the text to the console the program was started from (cmd,
// PowerShell). Returns false when there is no such console: the caller then
// says the same thing its own way.
bool WriteToParentConsole(const std::string& text) {
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return false;
    }

    ::SetConsoleOutputCP(CP_UTF8);

    FILE* stream = nullptr;
    const bool redirected = ::freopen_s(&stream, "CONOUT$", "w", stdout) == 0 && stream != nullptr;

    if (redirected) {
        std::fwrite(text.data(), 1, text.size(), stream);
        std::fflush(stream);
    }

    ::FreeConsole();
    return redirected;
}

EDataFlow EffectiveRenderFlow(Config::DataFlow flow) {
    return flow == Config::DataFlow::Capture ? eCapture : eRender;
}

bool FlowMatches(Config::DataFlow configured, EDataFlow changed) {
    if (configured == Config::DataFlow::Both) {
        return changed == eRender || changed == eCapture;
    }

    return changed == EffectiveRenderFlow(configured);
}

}

App::App(HINSTANCE instance):
    m_instance(instance) {}

App::~App() {
    Shutdown();
}

int App::Run() {
    m_options = CommandLine::Parse();

    // A key the program does not know is reported like everything else about
    // the start: through the journal of the program.
    for (const auto& argument : m_options.unknown) {
        m_startupMessages.emplace_back(
            StartupLevel::Warn, fmt::format(Lang::Utf8(Str::LogUnknownArgument), argument));
    }

    for (const auto& error : m_options.errors) {
        m_startupMessages.emplace_back(StartupLevel::Warn, error);
    }

    if (m_options.action == CommandLine::Action::ShowHelp) {
        PrintStartupText(CommandLine::HelpText());
        return 0;
    }

    if (m_options.action == CommandLine::Action::ShowVersion) {
        PrintStartupText(CommandLine::VersionText());
        return 0;
    }

    if (m_options.action == CommandLine::Action::Diagnose) {
        return RunDiagnostics();
    }

    RegisterSignalMessages();

    int handOverCode = 0;
    if (HandOverToRunningInstance(handOverCode)) {
        return handOverCode;
    }

    // "--exit" without a running copy: there is nothing to close, and the
    // settings file and the log are not touched.
    if (m_options.action == CommandLine::Action::Exit) {
        return 0;
    }

    const bool settingsLoaded = LoadSettings();

    InitializeLogging();

    LogBanner();
    LogStartupMessages();

    if (!settingsLoaded) {
        Log::Warn(Lang::Utf8(m_settingsTooLarge ? Str::LogSettingsTooLargeDefaults : Str::LogSettingsUnreadable));
    }

    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        Log::Error(Lang::Utf8(Str::LogComFailed), Windows::DescribeHResult(static_cast<long>(comResult)));
        Shutdown();
        return 1;
    }

    m_comInitialized = true;

    ApplyPerformanceSettings();
    CleanupPreviousInstall();

    if (!InitializeUi()) {
        Shutdown();
        return 1;
    }

    ApplyStartWithWindows();

    InitializeAudio();
    InitializeTray();

    // A broken settings file is easy to miss when the program starts in the
    // tray: one balloon says so, the details are in the journal.
    if (!settingsLoaded && m_window != nullptr) {
        m_window->Notify(Lang::Wide(Str::NotifyTitle), Lang::Wide(Str::NotifySettingsUnreadable), true);
    }

    if (m_settings.updates.checkOnStartup) {
        StartUpdateCheck();
    }

    ::SetTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::Validate), VALIDATE_INTERVAL_MS, nullptr);

    MSG message = {};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    Shutdown();
    return m_exitCode;
}

bool App::LoadSettings() {
    m_settingsPath = m_options.configPath ? *m_options.configPath : Config::GetDefaultPath();

    // A file that cannot be parsed leaves the defaults in effect; the options
    // of the command line still apply.
    bool loaded = true;

    if (!m_options.ignoreConfig) {
        // The language has to be known before the file is read: everything the
        // reader says about it goes to the journal of the program, which is
        // written in the language of the interface.
        Lang::Set(Lang::FromCode(Config::PeekLanguage(m_settingsPath)));

        const Config::LoadResult result = Config::Load(m_settingsPath);
        m_settings = result.settings;

        // The language has to be known before anything is printed or written:
        // the settings file itself is created with comments in that language.
        Lang::Set(Lang::FromCode(m_settings.application.language));

        // The comments of the file belong to the language it was written in.
        // When the language changes (application.language or the language of
        // Windows for "auto"), the file is rewritten in the new language on the
        // next start; every value is taken from the loaded settings, so nothing
        // is lost but the comments.
        const std::string commentLanguage = Lang::Code(Lang::Current());

        for (const auto& warning : result.warnings) {
            m_startupMessages.emplace_back(
                StartupLevel::Warn, fmt::format("{} {}", Lang::Utf8(Str::SettingsPrefix), warning));
        }

        // A file larger than the limit makes room for a new file with the
        // defaults when it can be renamed; otherwise it stays as it is.
        bool createFile = !result.fileExists;

        if (result.tooLarge) {
            createFile = SetLargeSettingsFileAside(true);
            m_settingsTooLarge = !createFile;
        } else if (result.parseFailed) {
            m_startupMessages.emplace_back(
                StartupLevel::Error, fmt::format("{} {}", Lang::Utf8(Str::SettingsPrefix), result.error));
        }

        if (createFile) {
            m_settings.commentLanguage = commentLanguage;

            if (Config::Write(m_settings, m_settingsPath)) {
                m_startupMessages.emplace_back(
                    StartupLevel::Info,
                    fmt::format(Lang::Utf8(Str::OpSettingsCreated), Text::ToUtf8(m_settingsPath)));
            } else {
                m_startupMessages.emplace_back(
                    StartupLevel::Error,
                    fmt::format(Lang::Utf8(Str::LogSettingsWriteFailed), Text::ToUtf8(m_settingsPath)));
            }
        } else if (!result.parseFailed && (Config::PeekCommentLanguage(m_settingsPath) != commentLanguage ||
            m_settings.configVersion != Config::CONFIG_VERSION)) {
            // A file of an older layout is upgraded once, with every value
            // kept; a file of another comment language is rewritten with the
            // comments in the current one. The very same write does both.
            const bool languageChanged = Config::PeekCommentLanguage(m_settingsPath) != commentLanguage;

            m_settings.configVersion = Config::CONFIG_VERSION;
            m_settings.commentLanguage = commentLanguage;

            if (Config::Write(m_settings, m_settingsPath)) {
                // Logging is not available yet, the line is written below.
                m_commentsRewritten = commentLanguage;
                m_layoutUpgraded = !languageChanged;
            } else {
                m_startupMessages.emplace_back(
                    StartupLevel::Error,
                    fmt::format(Lang::Utf8(Str::LogSettingsWriteFailed), Text::ToUtf8(m_settingsPath)));
            }
        }

        // A renamed file is replaced by the defaults like a missing one; the
        // one that stays is not backed up as unreadable (".bad") either.
        loaded = !result.parseFailed || (result.tooLarge && !m_settingsTooLarge);
        m_settingsBroken = result.parseFailed && result.fileExists && !result.tooLarge;
    } else {
        Lang::Set(Lang::Detect());
    }

    m_fileSettings = m_settings;
    ApplyCommandLine(m_settings);

    return loaded;
}

void App::ApplyCommandLine(Config::Settings& settings) const {
    if (m_options.startMinimizedToTray) {
        settings.application.startMinimizedToTray = *m_options.startMinimizedToTray;
    }

    if (m_options.logLevel) {
        settings.logging.level = *m_options.logLevel;
    }
}

// Everything the reader of the settings file had to say, in the order it was
// said: the lines appear in the window and in the file like any other message.
void App::LogStartupMessages() {
    for (const auto& message : m_startupMessages) {
        switch (message.first) {
            case StartupLevel::Info:
                Log::Info("{}", message.second);
                break;
            case StartupLevel::Warn:
                Log::Warn("{}", message.second);
                break;
            case StartupLevel::Error:
                Log::Error("{}", message.second);
                break;
        }
    }

    m_startupMessages.clear();
}

bool App::InitializeLogging() {
    // The window of the program always shows the operations at the info level;
    // the log file is described by the logging settings alone.
    Log::Initialize(m_settings);
    return true;
}

void App::LogBanner() {
    Log::Operation(Lang::Utf8(Str::OpStarted), AppInfo::DisplayVersion());

    if (m_layoutUpgraded) {
        Log::Operation(Lang::Utf8(Str::OpSettingsUpgraded));
    } else if (!m_commentsRewritten.empty()) {
        Log::Operation(Lang::Utf8(Str::OpCommentsRewritten), m_commentsRewritten);
    }

    m_commentsRewritten.clear();
    m_layoutUpgraded = false;

    Log::Operation(Lang::Utf8(Str::OpSettingsFile), Text::ToUtf8(m_settingsPath));

    // The configuration that takes part in the latency reduction: this is what a
    // diagnostics report needs. The version is in the started line above, the
    // remaining settings are in the settings file.
    Log::Info(Lang::Utf8(Str::LogAudioSettings), Config::DescribeFlow(m_settings), Config::DescribeBuffer(m_settings));
}

bool App::InitializeUi() {
    auto window = Windows::MainWindow::Create(m_instance);
    if (!window) {
        Log::Error(Lang::Utf8(Str::LogWindowFailed), window.error().GetMessage());
        return false;
    }

    m_window = std::move(*window);

    // There is no tray icon yet: the buttons of the window behave as usual
    // until InitializeTray() has shown it.
    ApplyWindowBehaviour();
    m_window->ApplyLanguage();

    m_window->SetCommandHandler([this](Command command) {
        OnCommand(command);
        });

    m_window->SetMessageHandler([this](UINT message, WPARAM wParam, LPARAM lParam) {
        OnWindowMessage(message, wParam, lParam);
        });

    const HWND windowHandle = m_window->GetHWindow();

    // Needed for WM_WTSSESSION_CHANGE (re-initialise the audio streams after
    // the session has been unlocked).
    if (::WTSRegisterSessionNotification(windowHandle, NOTIFY_FOR_THIS_SESSION) == FALSE) {
        Log::Debug(Lang::Utf8(Str::LogSessionNotifications), Windows::DescribeLastError());
    }

    Log::Buffer().SetNotifyHandler([windowHandle]() {
        return ::PostMessageW(windowHandle, WM_APP_LOG_LINES, 0, 0) != FALSE;
        });

    m_window->AppendLogLines(Log::Buffer().TakePending());

    // Without the tray icon the window always starts visible. The setting of
    // the file is greyed out in the settings window then, so only an explicit
    // --tray is worth a line.
    if (!m_settings.tray.enabled && m_settings.application.startMinimizedToTray) {
        if (m_options.startMinimizedToTray.value_or(false)) {
            Log::Warn(Lang::Utf8(Str::LogTrayHidden));
        }

        m_settings.application.startMinimizedToTray = false;
    }

    if (m_settings.application.startMinimizedToTray) {
        m_window->Hide();
    } else {
        m_window->Show();
    }

    return true;
}

void App::InitializeAudio() {
    const HWND windowHandle = m_window->GetHWindow();

    auto initialized = m_audio.Initialize([windowHandle](const Windows::DeviceEvent& event) {
        ::PostMessageW(
            windowHandle,
            WM_APP_DEVICE_EVENT,
            0,
            MAKELPARAM(static_cast<WORD>(event.dataFlow), static_cast<WORD>(event.role)));
        });

    if (!initialized) {
        Log::Error("{}", initialized.error().GetMessage());
        if (m_settings.tray.notifications.onError) {
            m_window->Notify(
                std::wstring(AppInfo::NAME), Text::ToWide(initialized.error().GetMessage()), true);
        }

        m_audioEnabled = false;
        UpdateStatus();
        return;
    }

    // The latency reduction is always on at the start: switching it off is a
    // decision for the running session (the menus, --disable).
    m_audioEnabled = true;
    ApplyAudio();
}

void App::InitializeTray() {
    if (m_settings.tray.enabled) {
        m_window->EnableTray(true);

        if (!m_window->IsTrayVisible()) {
            // Without a tray icon a hidden window could not be brought back, so
            // the window becomes the only way to control the application.
            Log::Operation(Lang::Utf8(Str::OpTrayUnavailable));

            if (!m_window->IsVisible()) {
                m_window->Show();
            }
        }
    }

    ApplyWindowBehaviour();

    if (m_window->IsTrayVisible()) {
        UpdateTrayMenuState();
        UpdateStatus();
    }
}

// The buttons of the window hide it in the tray only when there is a tray icon
// to bring it back from; without the icon "Minimize" and the close button do
// what they always do (the settings of the file are greyed out in the window).
void App::ApplyWindowBehaviour() {
    if (!m_window) {
        return;
    }

    const bool tray = m_window->IsTrayVisible();

    m_window->SetMinimizeToTray(tray && m_settings.application.minimizeToTray);
    m_window->SetHideOnClose(tray && m_settings.application.closeButtonAction == Config::CloseAction::Minimize);
}

void App::RegisterSignalMessages() {
    m_signalShow = ::RegisterWindowMessageW(SIGNAL_SHOW);
    m_signalReinitialize = ::RegisterWindowMessageW(SIGNAL_REINITIALIZE);
    m_signalEnable = ::RegisterWindowMessageW(SIGNAL_ENABLE);
    m_signalDisable = ::RegisterWindowMessageW(SIGNAL_DISABLE);
    m_signalExit = ::RegisterWindowMessageW(SIGNAL_EXIT);
}

// One copy of the program runs at a time. The check comes before the settings
// are read: a second start only passes its command on and never touches the
// settings file or the log of the running copy.
bool App::HandOverToRunningInstance(int& exitCode) {
    exitCode = 0;

    // The lines written before the settings are read (by this check or by a
    // second start) are in the language of the settings; the file is only
    // peeked at.
    if (!m_options.ignoreConfig) {
        const std::wstring path = m_options.configPath ? *m_options.configPath : Config::GetDefaultPath();
        Lang::Set(Lang::FromCode(Config::PeekLanguage(path)));
    } else {
        Lang::Set(Lang::Detect());
    }

    const Windows::SingleInstance::State state = m_singleInstance.Acquire(INSTANCE_MUTEX_NAME);

    if (state == Windows::SingleInstance::State::Unknown) {
        // Without the mutex another copy cannot be detected: the program starts
        // and says why in the journal (the log does not exist yet, the line waits).
        const std::string error = Windows::DescribeError(m_singleInstance.Error());
        m_startupMessages.emplace_back(StartupLevel::Warn, fmt::format(Lang::Utf8(Str::LogMutexFailed), error));
        return false;
    }

    if (state == Windows::SingleInstance::State::First) {
        return false;
    }

    UINT signal = m_signalShow;

    switch (m_options.action) {
        case CommandLine::Action::Reinitialize: signal = m_signalReinitialize; break;
        case CommandLine::Action::Enable: signal = m_signalEnable; break;
        case CommandLine::Action::Disable: signal = m_signalDisable; break;
        case CommandLine::Action::Exit: signal = m_signalExit; break;
        default: break;
    }

    const ULONGLONG deadline = ::GetTickCount64() + HAND_OVER_WAIT_MS;

    for (;;) {
        const Windows::SignalResult result =
            Windows::SignalRunningInstance(Windows::MAIN_WINDOW_CLASS_NAME, signal, SIGNAL_TIMEOUT_MS);

        if (result == Windows::SignalResult::Delivered) {
            // The running copy shows its window; a console the command was
            // typed in gets one line about it.
            if (m_options.action == CommandLine::Action::Run) {
                WriteToParentConsole(std::string(Lang::Utf8(Str::OpAlreadyRunning)) + "\n");
            }

            return true;
        }

        if (result == Windows::SignalResult::NotResponding) {
            break;
        }

        // No window: the running copy is ending (or is still starting). Once it
        // has ended, this start becomes the running copy; "--exit" has nothing
        // left to close then.
        if (m_singleInstance.WaitForOwnership(HAND_OVER_POLL_MS)) {
            return m_options.action == CommandLine::Action::Exit;
        }

        if (::GetTickCount64() >= deadline) {
            break;
        }
    }

    // The running copy does not answer. A second copy is never started:
    // "--exit" ends quietly, any other start says why nothing happened.
    if (m_options.action != CommandLine::Action::Exit) {
        ReportInstanceNotResponding();
        exitCode = 1;
    }

    return true;
}

void App::ReportInstanceNotResponding() const {
    const std::string text = Lang::Utf8(Str::OpInstanceNotResponding);

    if (!WriteToParentConsole(text + "\n")) {
        ::MessageBoxW(nullptr, Text::ToWide(text).c_str(), std::wstring(AppInfo::NAME).c_str(), MB_OK | MB_ICONWARNING);
    }
}

void App::ApplyPerformanceSettings() {
    HANDLE process = ::GetCurrentProcess();

    DWORD priorityClass = NORMAL_PRIORITY_CLASS;
    if (m_settings.performance.processPriority == Config::ProcessPriority::BelowNormal) {
        priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    } else if (m_settings.performance.processPriority == Config::ProcessPriority::Idle) {
        priorityClass = IDLE_PRIORITY_CLASS;
    }

    if (::SetPriorityClass(process, priorityClass) == FALSE) {
        Log::Warn(Lang::Utf8(Str::LogPriorityFailed), Windows::DescribeLastError());
    }

    // The process of REAL only holds the audio streams and waits for events;
    // the sound itself is processed by the Windows Audio service. Windows 11 is
    // told that this work is not urgent (EcoQoS: a lower clock, the efficient
    // cores). Windows 10 does not know the request, which changes nothing.
#if defined(PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
    PROCESS_POWER_THROTTLING_STATE state = {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    ::SetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state));
#endif
}

// One place for switching the mode on and off: the state of a failure is
// cleared, the retry timer is stopped and the program always applies the new
// state, so a command that asks for the mode that is already on still brings a
// visible result (the streams are created again).
void App::SetAudioEnabled(bool enabled) {
    m_audioEnabled = enabled;
    m_audioSuspended = false;
    m_failureSince = 0;

    // A switch the user asked for (a menu, a command, a restart): ApplyAudio()
    // shows the balloon about it, so there is one balloon per action.
    m_stateChangePending = true;

    // Switching the mode on is reported here and its result by ApplyAudio(),
    // switching it off by ApplyAudio() alone.
    if (m_audioEnabled) {
        Log::Operation(Lang::Utf8(Str::OpEnabled));
    }

    CancelAudioRetry();
    ApplyAudio();
}

void App::ApplyAudio() {
    if (!m_window) {
        return;
    }

    // A switch on or off is reported by one balloon, and only about the action
    // that asked for it: a restart of the program itself (the start, a device
    // event, the end of a sleep) shows none.
    const bool stateChange = m_stateChangePending && m_settings.tray.notifications.onStateChange;
    m_stateChangePending = false;

    if (!m_audioEnabled) {
        m_audio.Stop();
        Log::Operation(Lang::Utf8(Str::OpDisabled));

        if (stateChange) {
            m_window->Notify(Lang::Wide(Str::NotifyTitle), Lang::Wide(Str::StatusDisabled), false);
        }

        UpdateStatus();
        return;
    }

    auto result = m_audio.Apply(m_settings);
    if (!result) {
        const bool firstFailure = m_failureSince == 0;

        if (firstFailure) {
            // One message and one balloon per outage; the retries only go to
            // the file log, so that the window stays readable.
            m_failureSince = ::GetTickCount64();

            // The retries themselves stay quiet (see ScheduleAudioRetry). A
            // missing default device is no fault of the program but what keeps
            // it from working: a warning and no balloon - the only device has
            // been unplugged, or the sound of Windows is not ready yet after a
            // start, and the program goes on by itself once a device is there.
            // A device that is there and does not answer is an error.
            const bool noDevice = m_audio.HasNoDefaultDevice();

            if (noDevice) {
                Log::Warn("{}", result.error().GetMessage());
            } else {
                Log::Error("{}", result.error().GetMessage());
            }

            // The end of the outage gets a balloon only after this one.
            m_failureNotified = !noDevice && m_settings.tray.notifications.onError &&
                m_window->Notify(Lang::Wide(Str::NotifyAudioTitle), Lang::Wide(Str::NotifyDeviceNotReady), true);
        } else {
            Log::Debug("{}", result.error().GetMessage());
        }

        ScheduleAudioRetry();
        UpdateStatus();
        return;
    }

    // Success: any previous outage is over.
    const bool recovered = m_failureSince != 0;
    m_failureSince = 0;
    m_audioSuspended = false;
    CancelAudioRetry();

    // One line per apply: the technical result, or the reason why nothing has
    // to be held open (the driver has no smaller buffer than its default one).
    if (m_audio.IsActive()) {
        Log::Operation(Lang::Utf8(Str::OpApplied), m_audio.GetDetailsText());
    } else {
        Log::Operation(Lang::Utf8(Str::OpDriverMinimum), m_audio.GetDetailsText());
    }

    // The range of the buffer of the default device: the settings window shows
    // it next to the fixed buffer and checks the value against it.
    for (const auto& info : m_audio.GetStreams()) {
        if (info.minPeriod > 0) {
            m_bufferRange.deviceName = info.deviceName;
            m_bufferRange.sampleRate = info.sampleRate;
            m_bufferRange.minimum = info.minPeriod;
            m_bufferRange.maximum = (std::max)(info.minPeriod, info.maxPeriod);
            m_bufferRange.step = info.fundamentalPeriod;
            m_bufferRange.current = info.currentPeriod;
            break;
        }
    }

    // One balloon per action, and only about what the settings allow: a device
    // change, a switch on/off, or the end of an outage whose start had a
    // balloon of its own (a missing device has none, see above).
    const bool deviceChange = m_deviceChangePending && m_settings.tray.notifications.onDeviceChange;
    const bool recovery = recovered && m_failureNotified && m_settings.tray.notifications.onError;
    m_failureNotified = false;

    if (deviceChange || recovery || stateChange) {
        const std::wstring title = Lang::Wide(deviceChange ? Str::NotifyDeviceTitle : Str::NotifyTitle);
        m_window->Notify(title, m_audio.GetStatusText(), false);
    }

    m_deviceChangePending = false;
    UpdateStatus();
}

std::wstring App::CurrentOffStatusText() const {
    // The mode is off either because the user switched it off or because the
    // application gave up after a device stopped answering.
    return Lang::Wide(m_audioSuspended ? Str::StatusSuspended : Str::StatusDisabled);
}

void App::UpdateStatus() {
    if (!m_window) {
        return;
    }

    const std::wstring status = m_audioEnabled ? m_audio.GetStatusText() : CurrentOffStatusText();
    m_window->SetStatusText(status);
    m_window->SetTrayTooltip(std::wstring(AppInfo::NAME) + L" - " + status);
    UpdateTrayMenuState();
}

void App::UpdateTrayMenuState() {
    if (!m_window || !m_settings.tray.enabled) {
        return;
    }

    Windows::TrayMenuState state;
    state.enabled = m_audioEnabled;
    state.statusText = m_audioEnabled ? m_audio.GetStatusText() : CurrentOffStatusText();
    state.toggleEnabled = m_settings.tray.menu.toggleEnabled;
    state.reinitialize = m_settings.tray.menu.reinitialize;
    state.openLog = m_settings.tray.menu.openLog;
    state.diagnostics = m_settings.tray.menu.diagnostics;

    m_window->SetTrayMenuState(state);

    // The same two states are shown by the menu of the window itself.
    m_window->SetMenuChecks(m_audioEnabled, m_settings.application.startWithWindows);
}

void App::SaveSettings() {
    // A successful write is a normal event and needs no line of its own: the
    // file is the proof, and on a machine that starts with Windows the line
    // would be written on every start. Only a failure is worth reporting.
    if (!WriteSettingsFile()) {
        Log::Error(Lang::Utf8(Str::LogSettingsWriteFailed), Text::ToUtf8(m_settingsPath));
    }
}

bool App::WriteSettingsFile() {
    // A file larger than the limit (one that stayed at the start, or one that
    // has grown since) is renamed first; one that cannot be renamed is not
    // written over.
    unsigned long long size = 0;
    if (Windows::Filesystem::GetFileInfo(m_settingsPath, &size, nullptr) && size > Config::MAX_FILE_BYTES) {
        if (!SetLargeSettingsFileAside(false)) {
            return false;
        }

        m_settingsTooLarge = false;
        m_settingsBroken = false;
    }

    if (m_settingsBroken) {
        const std::wstring backup = m_settingsPath + L".bad";

        if (::CopyFileW(m_settingsPath.c_str(), backup.c_str(), FALSE) != FALSE) {
            Log::Warn(Lang::Utf8(Str::LogSettingsBackup), Text::ToUtf8(backup));
        }

        m_settingsBroken = false;
    }

    return Config::Write(m_fileSettings, m_settingsPath);
}

bool App::SetLargeSettingsFileAside(bool startup) {
    const std::wstring backup = m_settingsPath + L".bak";
    const std::string backupName = Text::ToUtf8(backup.substr(backup.find_last_of(L"\\/") + 1));

    // "Read-only" protects the file from the program as well.
    bool readOnly = false;
    unsigned long error = 0;
    bool renamed = false;
    std::string text;

    if (Windows::Filesystem::GetFileInfo(m_settingsPath, nullptr, &readOnly) && readOnly) {
        text = Lang::Utf8(Str::CfgTooLargeProtected);
    } else if (Windows::Filesystem::RenameFile(m_settingsPath, backup, &error)) {
        renamed = true;
        text = fmt::format(Lang::Utf8(startup ? Str::CfgTooLargeRenamedDefaults : Str::CfgTooLargeRenamed), backupName);
    } else {
        text = fmt::format(Lang::Utf8(Str::CfgTooLargeRenameFailed), Windows::SystemMessage(error));
    }

    text = fmt::format("{} {}", Lang::Utf8(Str::SettingsPrefix), text);

    if (startup) {
        m_startupMessages.emplace_back(renamed ? StartupLevel::Warn : StartupLevel::Error, text);
    } else if (renamed) {
        Log::Warn("{}", text);
    } else {
        Log::Error("{}", text);
    }

    return renamed;
}

void App::RefreshCommentsLanguage() {
    const std::string language = Lang::Code(Lang::Current());
    if (m_settings.commentLanguage == language) {
        return;
    }

    // Only the comments change: the settings themselves are written as loaded.
    m_settings.commentLanguage = language;
    m_fileSettings.commentLanguage = language;

    if (WriteSettingsFile()) {
        Log::Operation(Lang::Utf8(Str::OpCommentsRewritten), language);
    } else {
        Log::Error(Lang::Utf8(Str::LogSettingsWriteFailed), Text::ToUtf8(m_settingsPath));
    }
}

// Everything the program has to do after the settings have changed, no matter
// where the change came from (the window of the program or the file itself).
void App::ApplySettings(const Config::Settings& previous) {
    if (previous.application.language != m_settings.application.language) {
        Lang::Set(Lang::FromCode(m_settings.application.language));
        Log::Info(Lang::Utf8(Str::LogLanguageChanged), Lang::Code(Lang::Current()));
        RefreshCommentsLanguage();
    }

    if (m_window) {
        m_window->ApplyLanguage();

        if (m_settings.tray.enabled != previous.tray.enabled) {
            m_window->EnableTray(m_settings.tray.enabled);
        }

        // The icon was switched off while the window was in the tray: the window
        // comes back, nothing else could bring it back.
        if (!m_window->IsTrayVisible() && !m_window->IsVisible()) {
            m_window->Show();
        }

        ApplyWindowBehaviour();
    }

    // The autostart and the priority of the process are applied by the program
    // itself whenever they change, so the settings window does not have to talk
    // about them.
    if (m_settings.application.startWithWindows != previous.application.startWithWindows) {
        SetStartWithWindows(m_settings.application.startWithWindows);
    } else {
        // The command line of the entry says how the program starts ("--tray"):
        // when the start mode changes, the entry is corrected.
        ApplyStartWithWindows();
    }

    if (m_settings.performance.processPriority != previous.performance.processPriority) {
        ApplyPerformanceSettings();
    }

    UpdateTrayMenuState();

    Log::SetFileSettings(m_settings.logging);

    if (m_audioEnabled) {
        ApplyAudio();
    }
}

void App::ShowSettingsDialog() {
    // The window is modal and is never opened twice: "Settings" in the tray
    // menu brings the open one to the front instead.
    if (m_settingsWindowOpen) {
        Windows::ActivateSettingsWindow();
        return;
    }

    m_settingsWindowOpen = true;

    Config::Settings edited = m_fileSettings;

    const bool saved = Windows::ShowSettingsWindow(
        m_window != nullptr ? m_window->GetHWindow() : nullptr,
        m_instance,
        edited,
        m_settingsPath,
        m_bufferRange);

    m_settingsWindowOpen = false;

    if (!saved) {
        return;
    }

    // The window writes the file: the same writer keeps the comments and the
    // layout, so the file stays the source of truth.
    const Config::Settings previous = m_settings;
    m_fileSettings = edited;
    m_settings = edited;
    ApplyCommandLine(m_settings);
    SaveSettings();
    ApplySettings(previous);
}

void App::OpenLogFile() {
    // The journal is opened only when it is switched on and exists: an item that
    // silently writes a snapshot somewhere else is worse than a message that
    // says why there is nothing to open.
    if (Config::IsLogFileOff(m_settings.logging)) {
        ReportLogUnavailable(Str::LogFileOff);
        return;
    }

    std::wstring path = Text::ToWide(m_settings.logging.filePath);
    if (path.empty()) {
        path = L"REAL.log";
    }

    if (!Windows::Filesystem::IsAbsolutePath(path)) {
        path = Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), path);
    }

    if (!Windows::Filesystem::IsFile(path)) {
        ReportLogUnavailable(Str::LogFileMissing);
        return;
    }

    Log::Operation(Lang::Utf8(Str::OpLogOpened), Text::ToUtf8(path));

    // The sink of the file keeps the latest lines in its buffer: the file that
    // opens now has to show them (some lines, like the result of the update
    // check, are written to the file alone).
    Log::Flush();
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::ReportLogUnavailable(Lang::Str reason) {
    const std::wstring text = Lang::Wide(reason);

    // The window shows the line in its log view, and the user is told about the
    // reason the way the program tells about everything else.
    Log::Hint("{}", Lang::Utf8(reason));

    if (m_window != nullptr) {
        m_window->Notify(Lang::Wide(Str::NotifyTitle), text, false);
    } else {
        ::MessageBoxW(nullptr, text.c_str(), Lang::Wide(Str::NotifyTitle).c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

void App::ShowAboutDialog() {
    // A window of its own: the icon of the program, its name in large letters,
    // the note about the latency reduction and clickable links to the
    // documentation and to the project.
    Windows::ShowAboutWindow(
        m_window != nullptr ? m_window->GetHWindow() : nullptr,
        m_instance,
        m_settingsPath);
}

// The check reports to the log file only: a check that found nothing new, or
// could not check at all, is not worth a line in the window. A new version is
// shown in the window and by a balloon (see FinishUpdateCheck).
void App::StartUpdateCheck() {
    if (!m_settings.updates.checkOnStartup) {
        Log::FileOnly(Lang::Utf8(Str::LogUpdatesDisabled));
        return;
    }

    if (m_updateThread.joinable()) {
        Log::FileOnly(Lang::Utf8(Str::LogUpdateRunning));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_updateMessage.clear();
        m_updateUrl.clear();
    }

    Log::FileOnly(Lang::Utf8(Str::OpUpdateChecking));

    const std::string repository = AppInfo::GITHUB_REPOSITORY;
    const HWND windowHandle = m_window != nullptr ? m_window->GetHWindow() : nullptr;

    m_updateCancellation = std::make_shared<Http::Cancellation>();
    const std::shared_ptr<Http::Cancellation> cancellation = m_updateCancellation;

    m_updateThread = std::thread([this, repository, windowHandle, cancellation]() {
        AutoUpdater::AutoUpdater updater(repository);
        auto release = updater.GetLatestRelease(cancellation.get());

        std::string message;
        std::string details;
        std::string url;

        // A project without releases is a normal answer: the log file says so,
        // like it does for the latest version and for a failure.
        if (!release) {
            message = std::string(Lang::Utf8(Str::NotifyUpdateFailed));
            details = release.error();
        } else if (!release->published) {
            message = std::string(Lang::Utf8(Str::LogNoReleases));
        } else if (release->version > AppInfo::VERSION) {
            message = fmt::format(Lang::Utf8(Str::NotifyUpdateAvailable), release->version.ToString());
            url = release->releaseUrl;
        } else {
            message = fmt::format(Lang::Utf8(Str::NotifyUpdateLatest), AppInfo::DisplayVersion());
        }

        {
            std::lock_guard<std::mutex> lock(m_updateMutex);
            m_updateMessage = message;
            m_updateDetails = details;
            m_updateUrl = url;
        }

        if (windowHandle != nullptr) {
            ::PostMessageW(windowHandle, WM_APP_UPDATE_RESULT, 0, 0);
        }
        });
}

void App::CancelUpdateCheck() {
    if (m_updateCancellation) {
        m_updateCancellation->Cancel();
    }

    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
}

void App::FinishUpdateCheck() {
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }

    std::string message;
    std::string details;
    std::string url;

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        message = m_updateMessage;
        details = m_updateDetails;
        url = m_updateUrl;
    }

    if (message.empty()) {
        return;
    }

    // Only a new version is worth a line in the window and a balloon. A check
    // that failed stays in the log file: the program often starts with Windows
    // before the network is up, and the next start checks again.
    const bool hasUpdate = !url.empty();
    if (hasUpdate) {
        Log::Info("{} ({})", message, url);
    } else if (!details.empty()) {
        Log::FileOnly("{}: {}", message, details);
    } else {
        Log::FileOnly("{}", message);
    }

    if (m_window != nullptr && hasUpdate) {
        m_window->Notify(
            Lang::Wide(Str::NotifyTitle), Text::ToWide(message + "\n" + Lang::Utf8(Str::NotifyUpdateClick)), false);
    }
}

void App::OnCommand(Command command) {
    switch (command) {
        case Command::ToggleEnabled:
            SetAudioEnabled(!m_audioEnabled);
            break;

        case Command::Reinitialize:
            // "Reinitialize now" always does something visible: it enables the
            // latency reduction again when it was off and applies it.
            SetAudioEnabled(true);
            break;

        case Command::OpenSettings:
            ShowSettingsDialog();
            break;

        case Command::OpenLog:
            OpenLogFile();
            break;

        case Command::Diagnose:
            ShowDiagnostics();
            break;

        case Command::ToggleStartWithWindows: {
            const bool enable = !m_fileSettings.application.startWithWindows;
            m_settings.application.startWithWindows = enable;
            m_fileSettings.application.startWithWindows = enable;
            SetStartWithWindows(enable);
            SaveSettings();
            UpdateTrayMenuState();
            break;
        }

        case Command::ShowWindow:
            if (m_window != nullptr) {
                m_window->Show();
            }

            break;

        case Command::ToggleWindow:
            if (m_window != nullptr) {
                m_window->Toggle();
            }

            break;

        case Command::BalloonClicked:
            if (!m_updateUrl.empty()) {
                ::ShellExecuteW(nullptr, L"open", Text::ToWide(m_updateUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (m_window != nullptr) {
                m_window->Show();
            }

            break;

        case Command::About:
            ShowAboutDialog();
            break;

        case Command::Exit:
            m_exitCode = 0;
            Log::Operation(Lang::Utf8(Str::OpExiting));

            // Everything that matters is done before the window goes: when
            // Windows ends the session (WM_ENDSESSION), the process may be ended
            // as soon as the window has answered.
            StopWork();

            if (m_window != nullptr) {
                ::DestroyWindow(m_window->GetHWindow());
            }

            break;

        default:
            break;
    }
}

void App::OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TIMER) {
        OnTimer(static_cast<UINT_PTR>(wParam));
        return;
    }

    if (message == WM_APP_DEVICE_EVENT) {
        OnDeviceEvent(lParam);
        return;
    }

    if (message == WM_POWERBROADCAST) {
        // The audio engine is reset when the machine leaves a sleep state; the
        // streams have to be re-created or the buffer size stays at its default.
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            OnSystemResume(false, Lang::Wide(Str::ReasonResume));
        }

        return;
    }

    if (message == WM_WTSSESSION_CHANGE) {
        if (wParam == WTS_SESSION_UNLOCK) {
            OnSystemResume(true, Lang::Wide(Str::ReasonUnlock));
        }

        return;
    }

    if (message == WM_APP_UPDATE_RESULT) {
        FinishUpdateCheck();
        return;
    }

    if (message == m_signalShow) {
        OnCommand(Command::ShowWindow);
        return;
    }

    if (message == m_signalReinitialize) {
        OnCommand(Command::Reinitialize);
        return;
    }

    if (message == m_signalEnable) {
        SetAudioEnabled(true);
        return;
    }

    if (message == m_signalDisable) {
        SetAudioEnabled(false);
        return;
    }

    if (message == m_signalExit) {
        OnCommand(Command::Exit);
        return;
    }
}

void App::OnTimer(UINT_PTR timerId) {
    if (timerId == static_cast<UINT_PTR>(TimerId::Validate)) {
        if (!m_audioEnabled || m_audioSuspended) {
            // Nothing is applied and the application has stopped polling until
            // a device event arrives (see ScheduleAudioRetry).
            return;
        }

        auto valid = m_audio.Validate();
        if (!valid) {
            Log::Info(Lang::Utf8(Str::LogReinitInvalid), valid.error().GetMessage());
            ApplyAudio();
            return;
        }

        return;
    }

    if (timerId == static_cast<UINT_PTR>(TimerId::AudioRetry)) {
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));

        if (!m_audioEnabled || m_audio.IsActive()) {
            return;
        }

        // The attempt itself does not need a line: ScheduleAudioRetry() wrote
        // the delay, and the result follows from ApplyAudio().
        ApplyAudio();
        return;
    }

    if (timerId == static_cast<UINT_PTR>(TimerId::DeviceEvent)) {
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent));

        // The timer is shared by the device events and the end of a sleep or a
        // lock (see OnSystemResume): a burst of both ends in one restart, and a
        // device event among them makes it a device change.
        const bool deviceEvent = m_deviceEventPending;
        m_deviceEventPending = false;

        if (!deviceEvent) {
            // Only the system woke up or the session was unlocked: the streams
            // are created again, and this is no device change to notify about.
            if (!m_audioEnabled) {
                return;
            }

            CancelAudioRetry();
            m_failureSince = 0;

            Log::Operation(Lang::Utf8(Str::OpResumeApply), Text::ToUtf8(m_resumeReason));
            ApplyAudio();
            return;
        }

        CancelAudioRetry();

        // The default device has changed (a device became the default one,
        // or the default one went away): this is the moment to start over,
        // always including switching the latency reduction back on when it was
        // off (by the user or after the device stopped answering).
        m_failureSince = 0;
        m_audioSuspended = false;
        m_deviceChangePending = true;

        if (!m_audioEnabled) {
            m_audioEnabled = true;
            m_stateChangePending = true;
            Log::Operation(Lang::Utf8(Str::OpEnabled));
        }

        Log::Operation(Lang::Utf8(Str::OpDeviceChanged));
        ApplyAudio();
    }
}

// A default device has changed. Any role matters: the program keeps a stream on
// the default device of every role, so a change of the usual default device and
// a change of the communication one are both worth re-applying. A default
// device that goes away or comes back is reported the same way, so this is the
// only event of the devices the program needs (see DeviceNotificationClient).
void App::OnDeviceEvent(LPARAM lParam) {
    const EDataFlow flow = static_cast<EDataFlow>(LOWORD(lParam));

    if (!m_settings.audio.reinit.defaultDeviceChanged || !FlowMatches(m_settings.audio.dataFlow, flow)) {
        return;
    }

    m_deviceEventPending = true;
    ScheduleDeviceRestart();
}

// Windows sends a burst of events for one change, and the list of the devices
// is not ready right after a resume: the restart waits for the last event of a
// burst. A timer that is set again starts over.
void App::ScheduleDeviceRestart() {
    if (m_window == nullptr || m_workStopped) {
        return;
    }

    ::SetTimer(
        m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent),
        DebounceMs(m_settings.audio.reinit), nullptr);
}

void App::ScheduleAudioRetry() {
    if (m_window == nullptr || m_workStopped) {
        return;
    }

    const ULONGLONG now = ::GetTickCount64();
    if (m_failureSince == 0) {
        m_failureSince = now;
    }

    const unsigned int limit = FailureTimeoutMs(m_settings.audio.reinit);

    const ULONGLONG elapsed = now - m_failureSince;
    if (elapsed >= limit) {
        GiveUpOnDevice();
        return;
    }

    m_retryDelayMs = m_retryDelayMs == 0
        ? INITIAL_AUDIO_RETRY_MS
        : std::min(m_retryDelayMs * 2, MAXIMUM_AUDIO_RETRY_MS);

    // Never sleep past the deadline: the last attempt happens while the device
    // still has a chance to answer.
    const ULONGLONG remaining = limit - elapsed;
    unsigned int delay = static_cast<unsigned int>(std::min<ULONGLONG>(m_retryDelayMs, remaining));
    delay = std::max(delay, 500u);

    Log::Debug(Lang::Utf8(Str::OpDeviceNotReady), (delay + 999) / 1000);

    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));
    ::SetTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry), delay, nullptr);
}

void App::GiveUpOnDevice() {
    const unsigned int limit = FailureTimeoutMs(m_settings.audio.reinit);

    // Without any default device the program waits for one quietly (see
    // ApplyAudio): the balloon is for a device that is there and does not
    // answer.
    const bool noDevice = m_audio.HasNoDefaultDevice();

    m_retryDelayMs = 0;
    m_failureSince = 0;
    m_audioSuspended = true;
    m_audioEnabled = false;

    CancelAudioRetry();
    m_audio.Stop();

    Log::Operation(Lang::Utf8(Str::OpGaveUp), (limit + 999) / 1000);
    Log::Operation(Lang::Utf8(Str::OpDiagHint));

    if (m_settings.tray.notifications.onError && !noDevice) {
        m_window->Notify(Lang::Wide(Str::NotifyAudioTitle), Lang::Wide(Str::NotifyDisabledNoAnswer), true);
    }

    UpdateStatus();
}

void App::CancelAudioRetry() {
    m_retryDelayMs = 0;

    if (m_window == nullptr) {
        return;
    }

    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));
}

std::wstring App::WriteDiagnosticsReport(const std::string& report) {
    std::wstring path = Windows::Filesystem::JoinPath(
        Windows::Filesystem::GetExecutableDirectory(),
        DIAGNOSTICS_FILE_NAME);

    if (!Windows::Filesystem::WriteTextFileUtf8(path, report)) {
        // The installation directory may be read-only (e.g. Program Files).
        path = Windows::Filesystem::JoinPath(Windows::Filesystem::GetTempDirectory(), DIAGNOSTICS_FILE_NAME);
        if (!Windows::Filesystem::WriteTextFileUtf8(path, report)) {
            return {};
        }
    }

    return path;
}

int App::RunDiagnostics() {
    LoadSettings();
    InitializeLogging();
    LogStartupMessages();

    if (m_layoutUpgraded) {
        Log::Operation(Lang::Utf8(Str::OpSettingsUpgraded));
    } else if (!m_commentsRewritten.empty()) {
        Log::Operation(Lang::Utf8(Str::OpCommentsRewritten), m_commentsRewritten);
    }

    m_commentsRewritten.clear();
    m_layoutUpgraded = false;

    // One line about the diagnostics: the steps of the report stay at the
    // debug level, otherwise a single key press fills the log with four lines.
    const std::string report = Windows::Diagnostics::BuildReport(m_settings, m_settingsPath);

    const std::wstring path = WriteDiagnosticsReport(report);
    if (!path.empty()) {
        Log::Info(Lang::Utf8(Str::OpDiagnostics), Text::ToUtf8(path));
    } else {
        Log::Error(Lang::Utf8(Str::OpDiagnosticsFailed));
    }

    Log::Shutdown();

    std::string consoleText = report;
    if (!path.empty()) {
        consoleText += fmt::format(Lang::Utf8(Str::CliReportWritten), Text::ToUtf8(path)) + "\n";
    }

    if (WriteToParentConsole(consoleText)) {
        return path.empty() ? 1 : 0;
    }

    if (path.empty()) {
        const std::wstring message = Lang::Wide(Str::DiagnosticsWriteFailed);
        const std::wstring title = Lang::Wide(Str::NotifyTitle);

        ::MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}

void App::ShowDiagnostics() {
    const std::wstring path = WriteDiagnosticsReport(
        Windows::Diagnostics::BuildReport(m_settings, m_settingsPath));
    if (path.empty()) {
        Log::Error(Lang::Utf8(Str::OpDiagnosticsFailed));
        if (m_window != nullptr) {
            m_window->Notify(Lang::Wide(Str::NotifyTitle), Lang::Wide(Str::NotifyReportFailed), true);
        }

        return;
    }

    Log::Operation(Lang::Utf8(Str::OpDiagnostics), Text::ToUtf8(path));
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::OnSystemResume(bool sessionUnlock, const std::wstring& reason) {
    const bool relevant = sessionUnlock
        ? m_settings.audio.reinit.sessionUnlock
        : m_settings.audio.reinit.resumeFromSleep;

    if (!relevant || !m_audioEnabled) {
        return;
    }

    // The same pause as for the device notifications, with the same timer (the
    // line about the restart is written when it fires).
    m_resumeReason = reason;
    ScheduleDeviceRestart();
}

// The command line the entry has to contain: the program with the start mode
// of the settings file (an option of one run, "--tray" typed by hand, does not
// count).
std::wstring App::AutostartCommand() const {
    std::wstring command = L"\"" + Windows::Filesystem::GetExecutablePath() + L"\"";

    if (m_fileSettings.application.startMinimizedToTray) {
        command += L" --tray";
    }

    return command;
}

bool App::WriteAutostartCommand() {
    // The registry returns its error code instead of setting the last error.
    const unsigned long error = Windows::Autostart::Write(AutostartCommand());

    if (error != 0) {
        Log::Error(Lang::Utf8(Str::LogAutostartWriteFailed), Windows::DescribeError(error));
        return false;
    }

    return true;
}

void App::ApplyStartWithWindows() {
    const std::wstring current = Windows::Autostart::Read();

    if (!m_fileSettings.application.startWithWindows) {
        if (!current.empty()) {
            SetStartWithWindows(false);
        }

        return;
    }

    if (current == AutostartCommand()) {
        return;
    }

    // A missing entry is created and reported; an entry with another command
    // line (another start mode, the program was moved) is corrected quietly.
    if (current.empty()) {
        SetStartWithWindows(true);
    } else {
        WriteAutostartCommand();
    }
}

void App::SetStartWithWindows(bool enabled) {
    if (enabled) {
        if (WriteAutostartCommand()) {
            Log::Operation(Lang::Utf8(Str::OpAutostart), Lang::Utf8(Str::ValueOn));
        }

        return;
    }

    const unsigned long error = Windows::Autostart::Remove();

    if (error != 0) {
        Log::Error(Lang::Utf8(Str::LogAutostartRemoveFailed), Windows::DescribeError(error));
        return;
    }

    Log::Operation(Lang::Utf8(Str::OpAutostart), Lang::Utf8(Str::ValueOff));
}

void App::CleanupPreviousInstall() {
    std::string message;
    if (AutoUpdater::AutoUpdater::CleanupPreviousInstall(&message)) {
        Log::Info(Lang::Utf8(Str::LogUpdateLeftover));
    } else if (!message.empty()) {
        Log::Warn("{}", message);
    }
}

void App::PrintStartupText(const std::wstring& text) const {
    if (WriteToParentConsole(Text::ToUtf8(text) + "\n")) {
        return;
    }

    const std::wstring title = std::wstring(AppInfo::NAME) + L" " + Text::ToWide(AppInfo::DisplayVersion());
    ::MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void App::StopWork() {
    if (m_workStopped) {
        return;
    }

    m_workStopped = true;

    // A check of updates that waits for the network is interrupted, not
    // waited for.
    CancelUpdateCheck();

    if (m_window != nullptr) {
        const HWND window = m_window->GetHWindow();

        ::KillTimer(window, static_cast<UINT_PTR>(TimerId::Validate));
        ::KillTimer(window, static_cast<UINT_PTR>(TimerId::DeviceEvent));
        ::KillTimer(window, static_cast<UINT_PTR>(TimerId::AudioRetry));

        // Before the window is destroyed: afterwards the call fails.
        ::WTSUnRegisterSessionNotification(window);
    }

    m_audio.Shutdown();

    Log::Info(Lang::Utf8(Str::LogStopped));
    Log::Shutdown();
}

void App::Shutdown() {
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;

    StopWork();

    m_window.reset();

    if (m_comInitialized) {
        ::CoUninitialize();
        m_comInitialized = false;
    }

    // The next start may become the running copy from now on.
    m_singleInstance.Release();
}
