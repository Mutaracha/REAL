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
    Diagnose,
    ShowHelp,
    ShowVersion,
};

struct Options {
    Action action = Action::Run;
    std::optional<std::wstring> configPath;
    std::optional<std::string> logLevel;
    std::optional<bool> startMinimizedToTray;
    bool ignoreConfig = false;
    // Arguments the program does not know, as they were typed.
    std::vector<std::string> unknown;
    // Known options that came without their value ("--config" without a path)
    // or with a value that is not allowed (an unknown --log-level).
    std::vector<std::string> errors;
};

Options Parse();
std::wstring HelpText();
std::wstring VersionText();

}
