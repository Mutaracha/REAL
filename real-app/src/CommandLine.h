#pragma once

#include <optional>
#include <string>
#include <vector>

namespace miniant::CommandLine {

enum class Action {
    Run,
    Reinitialize,
    Enable,
    Disable,
    Exit,
    CheckForUpdates,
    ShowHelp,
    ShowVersion,
};

struct Options {
    Action action = Action::Run;
    std::optional<std::wstring> configPath;
    std::optional<std::string> logLevel;
    std::optional<bool> startMinimizedToTray;
    std::optional<bool> showConsole;
    std::optional<bool> singleInstance;
    bool ignoreConfig = false;
    std::vector<std::string> unknown;
};

Options Parse();
std::wstring HelpText();
std::wstring VersionText();

}
