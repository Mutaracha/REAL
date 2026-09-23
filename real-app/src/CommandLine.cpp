#include "CommandLine.h"

#include "AppVersion.h"
#include "Text.h"

#include <Windows.h>
#include <shellapi.h>

#include <sstream>

using namespace miniant::CommandLine;

namespace {

std::vector<std::wstring> GetArguments() {
    int count = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return {};
    }

    std::vector<std::wstring> result;
    for (int i = 1; i < count; ++i) {
        result.emplace_back(arguments[i]);
    }

    ::LocalFree(arguments);
    return result;
}

std::optional<bool> ParseFlag(const std::wstring& argument) {
    if (argument == L"--tray") {
        return true;
    }

    if (argument == L"--no-tray") {
        return false;
    }

    return {};
}

}

Options miniant::CommandLine::Parse() {
    Options options;
    const std::vector<std::wstring> arguments = GetArguments();

    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::wstring argument = arguments[i];
        const std::string argumentUtf8 = Text::ToLowerAscii(Text::ToUtf8(argument));

        if (argumentUtf8 == "--help" || argumentUtf8 == "-h" || argumentUtf8 == "-?" || argumentUtf8 == "/?") {
            options.action = Action::ShowHelp;
            continue;
        }

        if (argumentUtf8 == "--version") {
            options.action = Action::ShowVersion;
            continue;
        }

        if (argumentUtf8 == "--reinit" || argumentUtf8 == "--reinitialize") {
            options.action = Action::Reinitialize;
            continue;
        }

        if (argumentUtf8 == "--enable") {
            options.action = Action::Enable;
            continue;
        }

        if (argumentUtf8 == "--disable") {
            options.action = Action::Disable;
            continue;
        }

        if (argumentUtf8 == "--exit" || argumentUtf8 == "--quit") {
            options.action = Action::Exit;
            continue;
        }

        if (argumentUtf8 == "--diagnose" || argumentUtf8 == "--diag") {
            options.action = Action::Diagnose;
            continue;
        }

        if (argumentUtf8 == "--no-config") {
            options.ignoreConfig = true;
            continue;
        }

        if (argumentUtf8 == "--console") {
            options.showConsole = true;
            continue;
        }

        if (argumentUtf8 == "--no-console") {
            options.showConsole = false;
            continue;
        }

        if (argumentUtf8 == "--multi-instance") {
            options.singleInstance = false;
            continue;
        }

        if (argumentUtf8 == "--config") {
            if (i + 1 < arguments.size()) {
                options.configPath = arguments[++i];
            } else {
                options.unknown.push_back("--config requires a path");
            }

            continue;
        }

        if (argumentUtf8 == "--log-level") {
            if (i + 1 < arguments.size()) {
                options.logLevel = Text::ToLowerAscii(Text::ToUtf8(arguments[++i]));
            } else {
                options.unknown.push_back("--log-level requires a value");
            }

            continue;
        }

        const std::optional<bool> tray = ParseFlag(argument);
        if (tray) {
            options.startMinimizedToTray = *tray;
            continue;
        }

        options.unknown.push_back(argumentUtf8);
    }

    return options;
}

std::wstring miniant::CommandLine::HelpText() {
    std::wstringstream stream;
    stream << AppInfo::NAME << L" - " << AppInfo::DESCRIPTION << L" " << AppInfo::VERSION.ToString().c_str() << L"\n\n";
    stream << L"Usage: REAL.exe [options]\n\n";
    stream << L"  (no options)          Start with the main window; the tray icon is created as well\n";
    stream << L"  --tray                Start minimised to the system tray\n";
    stream << L"  --no-tray             Start with the main window visible\n";
    stream << L"  --console             Also attach a console window for the log output\n";
    stream << L"  --config <path>       Use the given settings file instead of real.settings.json\n";
    stream << L"  --no-config           Ignore the settings file, use built-in defaults\n";
    stream << L"  --log-level <level>   trace | debug | info | warn | error | off\n";
    stream << L"  --multi-instance      Do not reuse an already running instance\n";
    stream << L"\nCommands for a running instance (the command is forwarded to it and this process exits):\n";
    stream << L"  --reinit              Re-initialise the audio streams (e.g. after a device change)\n";
    stream << L"  --enable              Enable the latency reduction\n";
    stream << L"  --disable             Disable the latency reduction (audio engine returns to its default)\n";
    stream << L"  --exit                Close the running instance\n";
    stream << L"\nDiagnostics:\n";
    stream << L"  --diagnose            Write a report about the audio devices and drivers (REAL-diagnostics.txt)\n";
    stream << L"\n  --help, -h, /?        Show this help\n";
    stream << L"  --version             Show the version\n";
    stream << L"\nSettings: real.settings.json next to REAL.exe (created on the first run).\n";
    stream << L"Every parameter is documented by comments inside that file,\n";
    stream << L"see also docs/CONFIG.md in the repository.\n";
    return stream.str();
}

std::wstring miniant::CommandLine::VersionText() {
    std::wstringstream stream;
    stream << AppInfo::NAME << L" " << AppInfo::VERSION.ToString().c_str() << L" ("
           << AppInfo::DESCRIPTION << L")";
    return stream.str();
}
