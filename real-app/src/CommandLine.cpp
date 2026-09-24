#include "CommandLine.h"

#include "AppVersion.h"
#include "Lang.h"
#include "Text.h"

#include <Windows.h>
#include <shellapi.h>

#include <spdlog/fmt/fmt.h>

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
                options.unknown.push_back(Lang::Utf8(Lang::Str::ArgConfigNeedsPath));
            }

            continue;
        }

        if (argumentUtf8 == "--log-level") {
            if (i + 1 < arguments.size()) {
                options.logLevel = Text::ToLowerAscii(Text::ToUtf8(arguments[++i]));
            } else {
                options.unknown.push_back(Lang::Utf8(Lang::Str::ArgLogLevelNeedsValue));
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
    // The whole help is one translated block: the layout of the option table
    // differs between languages, so it cannot be assembled from parts.
    const std::string text = fmt::format(
        miniant::Lang::Utf8(miniant::Lang::Str::HelpText),
        miniant::Text::ToUtf8(AppInfo::NAME),
        miniant::Text::ToUtf8(AppInfo::DESCRIPTION),
        AppInfo::VERSION.ToString());

    return miniant::Text::ToWide(text);
}

std::wstring miniant::CommandLine::VersionText() {
    std::wstringstream stream;
    stream << AppInfo::NAME << L" " << AppInfo::VERSION.ToString().c_str() << L" ("
           << AppInfo::DESCRIPTION << L")";
    return stream.str();
}
