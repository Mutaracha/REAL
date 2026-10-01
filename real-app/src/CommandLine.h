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
    std::optional<bool> singleInstance;
    bool ignoreConfig = false;
    // Arguments the program does not know, as they were typed.
    std::vector<std::string> unknown;
    // Known options that came without their value ("--config" without a path).
    std::vector<std::string> errors;
};

Options Parse();
std::wstring HelpText();
std::wstring VersionText();

}
