#include "App.h"

#include <wtsapi32.h>

#include <cwchar>

#include "AppMessages.h"
#include "AppVersion.h"
#include "AutoUpdater.h"
#include "Log.h"
#include "Text.h"
#include "Windows/Console.h"
#include "Windows/Diagnostics.h"
#include "Windows/Filesystem.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace miniant;

namespace {

// The first retry happens quickly, later ones slow down; the endpoint usually
// becomes available again as soon as the device has been switched on.
constexpr unsigned int INITIAL_AUDIO_RETRY_MS = 2000;
constexpr unsigned int MAXIMUM_AUDIO_RETRY_MS = 30000;
constexpr const wchar_t* DIAGNOSTICS_FILE_NAME = L"REAL-diagnostics.txt";

}

namespace {

const wchar_t RUN_KEY_PATH[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t RUN_VALUE_NAME[] = L"REAL";

const wchar_t SIGNAL_SHOW[] = L"REAL.Signal.Show";
const wchar_t SIGNAL_REINITIALIZE[] = L"REAL.Signal.Reinitialize";
const wchar_t SIGNAL_ENABLE[] = L"REAL.Signal.Enable";
const wchar_t SIGNAL_DISABLE[] = L"REAL.Signal.Disable";
const wchar_t SIGNAL_EXIT[] = L"REAL.Signal.Exit";

const UINT VALIDATE_INTERVAL_MS = 30000;

struct HotkeyDefinition {
    UINT modifiers = 0;
    UINT virtualKey = 0;
    bool valid = false;
};

UINT ModifierFromName(const std::string& name) {
    if (name == "ctrl" || name == "control") {
        return MOD_CONTROL;
    }

    if (name == "alt") {
        return MOD_ALT;
    }

    if (name == "shift") {
        return MOD_SHIFT;
    }

    if (name == "win" || name == "windows") {
        return MOD_WIN;
    }

    return 0;
}

HotkeyDefinition ParseHotkey(const std::string& text) {
    HotkeyDefinition result;

    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == '+') {
            parts.push_back(Text::Trim(current));
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) {
        parts.push_back(Text::Trim(current));
    }

    if (parts.empty()) {
        return result;
    }

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const UINT modifier = ModifierFromName(Text::ToLowerAscii(parts[i]));
        if (modifier == 0) {
            return result;
        }

        result.modifiers |= modifier;
    }

    const std::string key = Text::ToLowerAscii(parts.back());
    if (key.size() == 1) {
        const char c = key[0];
        if (c >= 'a' && c <= 'z') {
            result.virtualKey = static_cast<UINT>('A' + (c - 'a'));
        } else if (c >= '0' && c <= '9') {
            result.virtualKey = static_cast<UINT>('0' + (c - '0'));
        }
    } else if (key.size() >= 2 && key[0] == 'f') {
        const int number = std::atoi(key.c_str() + 1);
        if (number >= 1 && number <= 24) {
            result.virtualKey = static_cast<UINT>(VK_F1 + number - 1);
        }
    }

    result.valid = result.virtualKey != 0 && result.modifiers != 0;
    return result;
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

ERole EffectiveRole(Config::DeviceRole role) {
    switch (role) {
        case Config::DeviceRole::Multimedia: return eMultimedia;
        case Config::DeviceRole::Communications: return eCommunications;
        default: return eConsole;
    }
}

}

App::App(HINSTANCE instance):
    m_instance(instance) {}

App::~App() {
    Shutdown();
}

int App::Run() {
    m_options = CommandLine::Parse();

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

    const bool settingsLoaded = LoadSettings();

    if (m_settings.application.singleInstance) {
        m_instanceMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\REAL.SingleInstance");
        if (m_instanceMutex == nullptr) {
            Log::Warn("Could not create the single instance mutex: {}", Windows::DescribeLastError());
        } else if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            m_anotherInstanceRuns = true;
        }
    }

    RegisterSignalMessages();

    if (m_anotherInstanceRuns) {
        UINT signal = m_signalShow;

        switch (m_options.action) {
            case CommandLine::Action::Reinitialize: signal = m_signalReinitialize; break;
            case CommandLine::Action::Enable: signal = m_signalEnable; break;
            case CommandLine::Action::Disable: signal = m_signalDisable; break;
            case CommandLine::Action::Exit: signal = m_signalExit; break;
            default: break;
        }

        if (NotifyRunningInstance(signal)) {
            if (m_options.action == CommandLine::Action::Run) {
                std::cout << "REAL is already running: using the running instance." << std::endl;
            }

            return 0;
        }

        if (m_options.action == CommandLine::Action::Exit) {
            return 0;
        }

        Log::Warn("Another instance seems to be running but did not answer; starting a new one.");
    }

    InitializeLogging();

    LogBanner();

    if (!settingsLoaded) {
        Log::Warn("The settings file could not be read; built-in default settings are used.");
    }

    if (m_options.action == CommandLine::Action::Exit) {
        return 0;
    }

    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        Log::Error("Could not initialise COM: {}", Windows::DescribeHResult(static_cast<long>(comResult)));
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

    InitializeAudio();
    InitializeTray();
    RegisterHotkeys();

    if (m_settings.updates.mode != Config::UpdatesMode::Off && m_settings.updates.checkOnStartup) {
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

    if (!m_options.ignoreConfig) {
        const Config::LoadResult result = Config::Load(m_settingsPath);
        m_settings = result.settings;

        for (const auto& warning : result.warnings) {
            std::cout << "Settings: " << warning << std::endl;
        }

        if (result.parseFailed) {
            std::cout << "Settings: " << result.error << std::endl;
        }

        if (!result.fileExists) {
            if (Config::Write(m_settings, m_settingsPath)) {
                std::cout << "Settings: created a template at " << Text::ToUtf8(m_settingsPath) << std::endl;
            } else {
                std::cout << "Settings: could not create " << Text::ToUtf8(m_settingsPath) << std::endl;
            }
        }

        if (result.parseFailed) {
            return false;
        }
    }

    if (m_options.startMinimizedToTray) {
        m_settings.application.startMinimizedToTray = *m_options.startMinimizedToTray;
    }

    if (m_options.showConsole) {
        m_settings.application.showConsole = *m_options.showConsole;
    }

    if (m_options.singleInstance) {
        m_settings.application.singleInstance = *m_options.singleInstance;
    }

    if (m_options.logLevel) {
        m_settings.logging.level = *m_options.logLevel;
    }

    return true;
}

bool App::InitializeLogging() {
    bool consoleAttached = false;

    if (m_settings.application.showConsole) {
        consoleAttached = Windows::Console::Attach();
        if (!consoleAttached) {
            std::cout << "Could not attach a console window." << std::endl;
        }
    }

    Log::Initialize(m_settings, consoleAttached);
    return true;
}

void App::LogBanner() {
    Log::Info("{} {} - {}, mini)(ant 2018-2019, fork maintained by Mutaracha", Text::ToUtf8(AppInfo::NAME), AppInfo::VERSION.ToString(), Text::ToUtf8(AppInfo::DESCRIPTION));
    Log::Info("Project: {}", AppInfo::PROJECT_URL);
    Log::Info("Upstream: {}", AppInfo::UPSTREAM_URL);
    Log::Info("Settings: {} ({})", Text::ToUtf8(m_settingsPath), Config::Describe(m_settings));
}

bool App::InitializeUi() {
    auto window = Windows::MainWindow::Create(m_instance);
    if (!window) {
        Log::Error("Could not create the main window: {}", window.error().GetMessage());
        return false;
    }

    m_window = std::move(*window);

    m_window->SetMinimizeToTray(m_settings.application.minimizeToTray);
    m_window->SetHideOnClose(m_settings.application.closeButtonAction == Config::CloseAction::Minimize);

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
        Log::Debug("Session notifications are not available: {}", Windows::DescribeLastError());
    }

    Log::Buffer().SetNotifyHandler([windowHandle]() {
        ::PostMessageW(windowHandle, WM_APP_LOG_LINES, 0, 0);
        });

    m_window->AppendLogLines(Log::Buffer().TakePending());

    if (!m_settings.tray.enabled && m_settings.application.startMinimizedToTray) {
        Log::Warn("The tray icon is disabled but the window would start hidden: showing the window.");
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
            static_cast<WPARAM>(static_cast<int>(event.type)),
            MAKELPARAM(static_cast<WORD>(event.dataFlow), static_cast<WORD>(event.role)));
        });

    if (!initialized) {
        Log::Error("{}", initialized.error().GetMessage());
        if (m_settings.tray.notifications.onError) {
            m_window->Notify(L"REAL", Text::ToWide(initialized.error().GetMessage()), true);
        }

        m_audioEnabled = false;
        UpdateStatus();
        return;
    }

    m_audioEnabled = m_settings.audio.enabledOnStartup;
    ApplyAudio();
}

void App::InitializeTray() {
    if (!m_settings.tray.enabled) {
        return;
    }

    m_window->EnableTray(true);

    if (!m_window->IsTrayVisible()) {
        // Without a tray icon a hidden window could not be brought back, so the
        // window becomes the only way to control the application.
        Log::Warn("The tray icon could not be created; the window stays visible.");
        m_window->SetMinimizeToTray(false);
        m_window->SetHideOnClose(false);
        return;
    }

    UpdateTrayMenuState();
    UpdateStatus();
}

void App::RegisterSignalMessages() {
    m_signalShow = ::RegisterWindowMessageW(SIGNAL_SHOW);
    m_signalReinitialize = ::RegisterWindowMessageW(SIGNAL_REINITIALIZE);
    m_signalEnable = ::RegisterWindowMessageW(SIGNAL_ENABLE);
    m_signalDisable = ::RegisterWindowMessageW(SIGNAL_DISABLE);
    m_signalExit = ::RegisterWindowMessageW(SIGNAL_EXIT);
}

bool App::NotifyRunningInstance(UINT message) const {
    if (message == 0) {
        return false;
    }

    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeoutW(
        HWND_BROADCAST,
        message,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_NORMAL,
        3000,
        &result);

    return sent != 0;
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
        Log::Warn("Could not change the process priority: {}", Windows::DescribeLastError());
    }

#if defined(PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
    if (m_settings.performance.disablePowerThrottling) {
        PROCESS_POWER_THROTTLING_STATE state = {};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

        if (::SetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state)) == FALSE) {
            // Windows 10 does not know this information class; it is not an error.
            Log::Debug("Power throttling could not be disabled: {}", Windows::DescribeLastError());
        } else {
            Log::Info("Power throttling (execution speed) disabled for this process.");
        }
    }
#else
    if (m_settings.performance.disablePowerThrottling) {
        Log::Debug("Power throttling is not available with the Windows SDK used for this build.");
    }
#endif
}

void App::ApplyAudio() {
    if (!m_window) {
        return;
    }

    if (!m_audioEnabled) {
        m_audio.Stop();
        Log::Info("Latency reduction is disabled; the audio engine returned to its default buffer size.");
        UpdateStatus();
        return;
    }

    auto result = m_audio.Apply(m_settings);
    if (!result) {
        Log::Error("{}", result.error().GetMessage());

        Log::Info("Run 'REAL.exe --diagnose' to write a report about the audio devices and drivers.");

        if (m_settings.tray.notifications.onError) {
            m_window->Notify(L"REAL - audio", Text::ToWide(result.error().GetMessage()), true);
        }

        m_lastApplyFailed = true;
        ScheduleAudioRetry();
    } else {
        m_lastApplyFailed = false;
        CancelAudioRetry();

        if (m_settings.tray.notifications.onStateChange) {
            m_window->Notify(L"REAL", m_audio.GetStatusText(), false);
        }
    }

    UpdateStatus();
}

void App::UpdateStatus() {
    if (!m_window) {
        return;
    }

    const std::wstring status = m_audioEnabled ? m_audio.GetStatusText() : L"Latency reduction is disabled";
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
    state.showStatus = m_settings.tray.menu.showStatus;
    state.statusText = m_audioEnabled ? m_audio.GetStatusText() : L"Latency reduction is disabled";
    state.toggleEnabled = m_settings.tray.menu.toggleEnabled;
    state.reinitialize = m_settings.tray.menu.reinitialize;
    state.openSettings = m_settings.tray.menu.openSettings;
    state.openLog = m_settings.tray.menu.openLog;
    state.startWithWindows = m_settings.tray.menu.startWithWindows;
    state.startWithWindowsChecked = IsStartWithWindowsEnabled();
    state.about = m_settings.tray.menu.about;
    state.exit = m_settings.tray.menu.exit;

    m_window->SetTrayMenuState(state);
}

void App::SaveSettings() {
    if (Config::Write(m_settings, m_settingsPath)) {
        Log::Info("Settings saved to {}.", Text::ToUtf8(m_settingsPath));
    } else {
        Log::Error("Could not write the settings file {}.", Text::ToUtf8(m_settingsPath));
    }
}

void App::ReloadSettings() {
    const Config::LoadResult result = Config::Load(m_settingsPath);
    if (result.parseFailed) {
        Log::Error("{}", result.error);
        return;
    }

    for (const auto& warning : result.warnings) {
        Log::Warn("Settings: {}", warning);
    }

    const bool wasConsoleEnabled = m_settings.application.showConsole;
    const Config::Settings previous = m_settings;
    m_settings = result.settings;

    if (previous.application.showConsole != wasConsoleEnabled) {
        Log::Warn("logging.toConsole / showConsole changes require a restart.");
    }

    if (m_window) {
        m_window->SetMinimizeToTray(m_settings.application.minimizeToTray);
        m_window->SetHideOnClose(m_settings.application.closeButtonAction == Config::CloseAction::Minimize);

        if (m_settings.tray.enabled != previous.tray.enabled) {
            m_window->EnableTray(m_settings.tray.enabled);
        }
    }

    Log::SetLevel(m_settings.logging.level);

    UnregisterHotkeys();
    RegisterHotkeys();

    if (m_audioEnabled) {
        ApplyAudio();
    }

    Log::Info("Settings reloaded: {}", Config::Describe(m_settings));
}

void App::OpenSettingsFile() {
    if (!Windows::Filesystem::IsFile(m_settingsPath)) {
        SaveSettings();
    }

    const HINSTANCE result = ::ShellExecuteW(
        nullptr,
        L"open",
        m_settingsPath.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        const std::wstring parameters = L"\"" + m_settingsPath + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"notepad.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void App::OpenLogFile() {
    std::wstring path;

    if (m_settings.logging.toFile && !m_settings.logging.filePath.empty()) {
        const std::wstring configured = Text::ToWide(m_settings.logging.filePath);
        path = configured.empty() ? L"" : configured;

        if (!path.empty() && !std::filesystem::path(path).is_absolute()) {
            path = Windows::Filesystem::JoinPath(Windows::Filesystem::GetExecutableDirectory(), path);
        }
    }

    if (path.empty() || !Windows::Filesystem::IsFile(path)) {
        path = Windows::Filesystem::JoinPath(Windows::Filesystem::GetTempDirectory(), L"REAL.log");
        if (!Log::WriteSnapshotToFile(path)) {
            Log::Error("Could not write the log snapshot to {}.", Text::ToUtf8(path));
            return;
        }
    }

    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::ShowAboutDialog() {
    const std::wstring text =
        std::wstring(AppInfo::NAME) + L" " + Text::ToWide(AppInfo::VERSION.ToString()) + L"\n" +
        std::wstring(AppInfo::DESCRIPTION) + L"\n\n" +
        L"While REAL is running, the Windows audio engine renders the default\n"
        L"playback device with the smallest buffer size supported by its driver.\n"
        L"The application can be closed to the system tray; use the tray menu or\n"
        L"the hotkey to re-initialise the audio streams after a device change.\n\n" +
        L"Settings: " + m_settingsPath + L"\n" +
        L"Project:  " + Text::ToWide(AppInfo::PROJECT_URL) + L"\n" +
        L"Upstream: " + Text::ToWide(AppInfo::UPSTREAM_URL);

    ::MessageBoxW(
        m_window != nullptr ? m_window->GetHWindow() : nullptr,
        text.c_str(),
        L"About REAL",
        MB_OK | MB_ICONINFORMATION);
}

void App::StartUpdateCheck() {
    if (m_settings.updates.mode == Config::UpdatesMode::Off) {
        Log::Info("Update checks are disabled (updates.mode = \"off\").");
        return;
    }

    if (m_updateThread.joinable()) {
        Log::Info("An update check is already running.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_updateMessage.clear();
        m_updateUrl.clear();
    }

    Log::Info("Checking for updates in '{}'...", m_settings.updates.repository);

    const std::string repository = m_settings.updates.repository;
    const HWND windowHandle = m_window != nullptr ? m_window->GetHWindow() : nullptr;

    m_updateThread = std::thread([this, repository, windowHandle]() {
        AutoUpdater::AutoUpdater updater(repository);
        auto release = updater.GetLatestRelease();

        std::string message;
        std::string url;

        if (!release) {
            message = std::string("Update check failed: ") + release.error();
        } else if (release->version > AppInfo::VERSION) {
            message = std::string("A newer release is available: ") + release->version.ToString();
            url = release->releaseUrl;
        } else {
            message = std::string("You are running the latest version (") + AppInfo::VERSION.ToString() + ").";
        }

        {
            std::lock_guard<std::mutex> lock(m_updateMutex);
            m_updateMessage = message;
            m_updateUrl = url;
        }

        if (windowHandle != nullptr) {
            ::PostMessageW(windowHandle, WM_APP_UPDATE_RESULT, 0, 0);
        }
        });
}

void App::FinishUpdateCheck() {
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }

    std::string message;
    std::string url;

    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        message = m_updateMessage;
        url = m_updateUrl;
    }

    if (message.empty()) {
        return;
    }

    const bool hasUpdate = !url.empty();
    if (hasUpdate) {
        Log::Info("{} ({})", message, url);
    } else {
        Log::Info("{}", message);
    }

    const bool isFailure = message.rfind("Update check failed", 0) == 0;

    if (m_window != nullptr && (hasUpdate || (isFailure && m_settings.tray.notifications.onError))) {
        const std::string balloonText = hasUpdate
            ? message + "\nClick this notification to open the release page."
            : message;

        m_window->Notify(L"REAL - updates", Text::ToWide(balloonText), isFailure);
    }
}

void App::OnCommand(Command command) {
    switch (command) {
        case Command::ToggleEnabled:
            m_audioEnabled = !m_audioEnabled;
            Log::Info("Latency reduction {}.", m_audioEnabled ? "enabled" : "disabled");
            CancelAudioRetry();
            ApplyAudio();
            break;

        case Command::Reinitialize:
            if (!m_audioEnabled) {
                // "Reinitialize now" always does something visible: it enables
                // the latency reduction again and applies it.
                Log::Info("The latency reduction was disabled; enabling it and reinitialising the audio streams.");
                m_audioEnabled = true;
            } else {
                Log::Info("Reinitialising the audio streams...");
            }

            CancelAudioRetry();
            ApplyAudio();
            break;

        case Command::OpenSettings:
            OpenSettingsFile();
            break;

        case Command::OpenLog:
            OpenLogFile();
            break;

        case Command::Diagnose:
            ShowDiagnostics();
            break;

        case Command::ToggleStartWithWindows: {
            const bool enable = !IsStartWithWindowsEnabled();
            SetStartWithWindows(enable);
            m_settings.application.startWithWindows = enable;
            SaveSettings();
            UpdateTrayMenuState();
            break;
        }

        case Command::ReloadSettings:
            ReloadSettings();
            break;

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

        case Command::HideToTray:
            if (m_window != nullptr) {
                m_window->Hide();
            }

            break;

        case Command::Exit:
            m_exitCode = 0;
            Log::Info("Exiting...");
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

    if (message == WM_HOTKEY) {
        const int id = static_cast<int>(wParam);
        if (id == HOTKEY_ID_TOGGLE) {
            OnCommand(Command::ToggleEnabled);
        } else if (id == HOTKEY_ID_REINITIALIZE) {
            OnCommand(Command::Reinitialize);
        }

        return;
    }

    if (message == WM_APP_DEVICE_EVENT) {
        OnDeviceEvent(wParam, lParam);
        return;
    }

    if (message == WM_POWERBROADCAST) {
        // The audio engine is reset when the machine leaves a sleep state; the
        // streams have to be re-created or the buffer size stays at its default.
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            OnSystemResume(L"resume from sleep");
        }

        return;
    }

    if (message == WM_WTSSESSION_CHANGE) {
        if (wParam == WTS_SESSION_UNLOCK) {
            OnSystemResume(L"session unlock");
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
        m_audioEnabled = true;
        ApplyAudio();
        return;
    }

    if (message == m_signalDisable) {
        m_audioEnabled = false;
        ApplyAudio();
        return;
    }

    if (message == m_signalExit) {
        OnCommand(Command::Exit);
        return;
    }
}

void App::OnTimer(UINT_PTR timerId) {
    if (timerId == static_cast<UINT_PTR>(TimerId::Validate)) {
        if (!m_audioEnabled) {
            return;
        }

        auto valid = m_audio.Validate();
        if (!valid) {
            Log::Info("Reinitialising the audio streams: {}", valid.error().GetMessage());
            ApplyAudio();
            return;
        }

        if (m_lastApplyFailed) {
            Log::Info("The latency reduction could not be applied earlier; trying again.");
            ApplyAudio();
        }

        return;
    }

    if (timerId == static_cast<UINT_PTR>(TimerId::AudioRetry)) {
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));

        if (!m_audioEnabled || m_audio.IsActive()) {
            return;
        }

        Log::Info("Retrying to enable the low latency mode...");
        ApplyAudio();
        return;
    }

    if (timerId == static_cast<UINT_PTR>(TimerId::DeviceEvent)) {
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent));

        CancelAudioRetry();
        m_lastApplyFailed = false;
        Log::Info("Audio device change detected; re-applying the low latency mode...");
        ApplyAudio();

        if (m_settings.tray.notifications.onDeviceChange && m_audioEnabled) {
            m_window->Notify(L"REAL - audio device", m_audio.GetStatusText(), false);
        }
    }
}

void App::OnDeviceEvent(WPARAM wParam, LPARAM lParam) {
    const auto type = static_cast<Windows::DeviceEventType>(static_cast<int>(wParam));
    const EDataFlow flow = static_cast<EDataFlow>(LOWORD(lParam));
    const ERole role = static_cast<ERole>(HIWORD(lParam));

    bool interesting = false;

    switch (type) {
        case Windows::DeviceEventType::DefaultDeviceChanged:
            interesting = m_settings.audio.reinit.defaultDeviceChanged &&
                FlowMatches(m_settings.audio.dataFlow, flow) &&
                role == EffectiveRole(m_settings.audio.role);
            break;

        case Windows::DeviceEventType::DeviceStateChanged:
            interesting = m_settings.audio.reinit.deviceStateChanged && FlowMatches(m_settings.audio.dataFlow, flow);
            break;

        case Windows::DeviceEventType::DeviceAdded:
            interesting = m_settings.audio.reinit.deviceAdded && FlowMatches(m_settings.audio.dataFlow, flow);
            break;

        case Windows::DeviceEventType::DeviceRemoved:
            interesting = m_settings.audio.reinit.deviceRemoved && FlowMatches(m_settings.audio.dataFlow, flow);
            break;

        default:
            break;
    }

    if (!interesting) {
        return;
    }

    const int debounce = m_settings.audio.reinit.debounceMs > 0 ? m_settings.audio.reinit.debounceMs : 500;
    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent));
    ::SetTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent), static_cast<UINT>(debounce), nullptr);
}

void App::ScheduleAudioRetry() {
    if (m_window == nullptr || m_shuttingDown) {
        return;
    }

    m_retryDelayMs = m_retryDelayMs == 0
        ? INITIAL_AUDIO_RETRY_MS
        : std::min(m_retryDelayMs * 2, MAXIMUM_AUDIO_RETRY_MS);

    Log::Info("Another attempt to enable the low latency mode will be made in {} ms.", m_retryDelayMs);

    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));
    ::SetTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry), m_retryDelayMs, nullptr);
}

void App::CancelAudioRetry() {
    m_retryDelayMs = 0;

    if (m_window == nullptr) {
        return;
    }

    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));
}

std::wstring App::WriteDiagnosticsReport() {
    const std::wstring report = Windows::Diagnostics::BuildReport(m_settings, m_settingsPath);
    const std::string utf8 = Text::ToUtf8(report);

    std::wstring path = Windows::Filesystem::JoinPath(
        Windows::Filesystem::GetExecutableDirectory(),
        DIAGNOSTICS_FILE_NAME);

    if (!Windows::Filesystem::WriteTextFileUtf8(path, utf8)) {
        // The installation directory may be read-only (e.g. Program Files).
        path = Windows::Filesystem::JoinPath(Windows::Filesystem::GetTempDirectory(), DIAGNOSTICS_FILE_NAME);
        if (!Windows::Filesystem::WriteTextFileUtf8(path, utf8)) {
            return {};
        }
    }

    return path;
}

int App::RunDiagnostics() {
    LoadSettings();

    const std::wstring report = Windows::Diagnostics::BuildReport(m_settings, m_settingsPath);
    const std::string utf8 = Text::ToUtf8(report);
    const std::wstring path = WriteDiagnosticsReport();

    if (::AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
        ::SetConsoleOutputCP(CP_UTF8);

        FILE* stream = nullptr;
        ::freopen_s(&stream, "CONOUT$", "w", stdout);
        std::cout << utf8;
        if (!path.empty()) {
            std::cout << "Report written to " << Text::ToUtf8(path) << std::endl;
        }

        std::cout.flush();
        ::FreeConsole();
        return path.empty() ? 1 : 0;
    }

    if (path.empty()) {
        ::MessageBoxW(
            nullptr,
            L"The diagnostics report could not be written to a file.",
            AppInfo::NAME,
            MB_OK | MB_ICONERROR);
        return 1;
    }

    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}

void App::ShowDiagnostics() {
    const std::wstring path = WriteDiagnosticsReport();
    if (path.empty()) {
        Log::Error("The diagnostics report could not be written.");
        if (m_window != nullptr) {
            m_window->Notify(L"REAL - diagnostics", L"The diagnostics report could not be written.", true);
        }

        return;
    }

    Log::Info("Diagnostics report written to {}.", Text::ToUtf8(path));
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::OnSystemResume(const wchar_t* reason) {
    const bool relevant =
        std::wcscmp(reason, L"session unlock") == 0
            ? m_settings.audio.reinit.sessionUnlock
            : m_settings.audio.reinit.resumeFromSleep;

    if (!relevant || !m_audioEnabled) {
        return;
    }

    Log::Info("The system reported {}; re-applying the low latency mode...", Text::ToUtf8(reason));

    // The device list is not ready immediately after a resume; reuse the same
    // debounce timer as for device notifications.
    const int debounce = m_settings.audio.reinit.debounceMs > 0 ? m_settings.audio.reinit.debounceMs : 500;
    ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent));
    ::SetTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent), static_cast<UINT>(debounce), nullptr);
}

void App::RegisterHotkeys() {
    if (!m_settings.hotkeys.enabled || m_window == nullptr) {
        return;
    }

    const HWND windowHandle = m_window->GetHWindow();

    const HotkeyDefinition toggle = ParseHotkey(m_settings.hotkeys.toggleEnabled);
    if (toggle.valid) {
        if (::RegisterHotKey(windowHandle, HOTKEY_ID_TOGGLE, toggle.modifiers | MOD_NOREPEAT, toggle.virtualKey) == FALSE) {
            Log::Warn("Could not register the hotkey '{}'.", m_settings.hotkeys.toggleEnabled);
        } else {
            Log::Info("Hotkey '{}' enables/disables the latency reduction.", m_settings.hotkeys.toggleEnabled);
        }
    } else if (!m_settings.hotkeys.toggleEnabled.empty()) {
        Log::Warn("Could not parse the hotkey '{}'.", m_settings.hotkeys.toggleEnabled);
    }

    const HotkeyDefinition reinitialize = ParseHotkey(m_settings.hotkeys.reinitialize);
    if (reinitialize.valid) {
        if (::RegisterHotKey(windowHandle, HOTKEY_ID_REINITIALIZE, reinitialize.modifiers | MOD_NOREPEAT, reinitialize.virtualKey) == FALSE) {
            Log::Warn("Could not register the hotkey '{}'.", m_settings.hotkeys.reinitialize);
        } else {
            Log::Info("Hotkey '{}' reinitialises the audio streams.", m_settings.hotkeys.reinitialize);
        }
    } else if (!m_settings.hotkeys.reinitialize.empty()) {
        Log::Warn("Could not parse the hotkey '{}'.", m_settings.hotkeys.reinitialize);
    }
}

void App::UnregisterHotkeys() {
    if (m_window == nullptr) {
        return;
    }

    ::UnregisterHotKey(m_window->GetHWindow(), HOTKEY_ID_TOGGLE);
    ::UnregisterHotKey(m_window->GetHWindow(), HOTKEY_ID_REINITIALIZE);
}

bool App::IsStartWithWindowsEnabled() const {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG result = ::RegQueryValueExW(key, RUN_VALUE_NAME, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ;
}

void App::SetStartWithWindows(bool enabled) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        Log::Error("Could not open the registry key for the autostart entry: {}", Windows::DescribeLastError());
        return;
    }

    if (enabled) {
        std::wstring command = L"\"" + Windows::Filesystem::GetExecutablePath() + L"\"";
        if (m_settings.application.startMinimizedToTray) {
            command += L" --tray";
        }

        const LONG result = ::RegSetValueExW(
            key,
            RUN_VALUE_NAME,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));

        if (result == ERROR_SUCCESS) {
            Log::Info("REAL will start with Windows.");
        } else {
            Log::Error("Could not write the autostart entry: {}", Windows::DescribeLastError());
        }
    } else {
        ::RegDeleteValueW(key, RUN_VALUE_NAME);
        Log::Info("REAL will not start with Windows anymore.");
    }

    ::RegCloseKey(key);
}

void App::CleanupPreviousInstall() {
    std::string message;
    if (AutoUpdater::AutoUpdater::CleanupPreviousInstall(&message)) {
        Log::Info("Removed the leftover file from a previous update.");
    } else if (!message.empty()) {
        Log::Warn("{}", message);
    }
}

void App::PrintStartupText(const std::wstring& text) const {
    if (::AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
        ::SetConsoleOutputCP(CP_UTF8);

        FILE* stream = nullptr;
        ::freopen_s(&stream, "CONOUT$", "w", stdout);
        std::cout << Text::ToUtf8(text) << std::endl;
        std::cout.flush();
        ::FreeConsole();
        return;
    }

    const std::wstring title = std::wstring(AppInfo::NAME) + L" " + Text::ToWide(AppInfo::VERSION.ToString());
    ::MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void App::Shutdown() {
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;

    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }

    UnregisterHotkeys();

    if (m_window != nullptr) {
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::Validate));
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::DeviceEvent));
        ::KillTimer(m_window->GetHWindow(), static_cast<UINT_PTR>(TimerId::AudioRetry));
        ::WTSUnRegisterSessionNotification(m_window->GetHWindow());
    }

    m_audio.Shutdown();

    Log::Info("REAL stopped.");
    Log::Shutdown();

    m_window.reset();

    if (m_comInitialized) {
        ::CoUninitialize();
        m_comInitialized = false;
    }

    Windows::Console::Detach();

    if (m_instanceMutex != nullptr) {
        ::ReleaseMutex(m_instanceMutex);
        ::CloseHandle(m_instanceMutex);
        m_instanceMutex = nullptr;
    }
}
