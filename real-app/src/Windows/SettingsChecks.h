#pragma once

#include "../Lang.h"
#include "../Settings.h"
#include "SettingsWindow.h"

#include <cstdint>
#include <string>

namespace miniant::Windows::SettingsChecks {

// The checks of the values typed into the settings window. Every check returns
// what is wrong with a value - the text of the balloon at its field, in the
// language the window shows - or an empty text when the window takes the
// value.

// A whole number typed into a field: digits only (a field of a number takes
// nothing else, but the clipboard can bring anything) and not more of them
// than any limit of the window has.
bool ParseNumber(const std::wstring& text, long& value);

// A number within the limits of the settings file.
std::wstring NumberProblem(const std::wstring& text, const Config::NumberLimits& limits, Lang::Language language);

// The step of the grid of the device; a driver that reports none takes every
// value of its range.
uint32_t BufferStep(const BufferRange& range);

// A size of the fixed buffer: the device takes it when it is inside its range
// and on its grid (a multiple of the step). While no device is known, any size
// of the settings file is taken.
std::wstring FixedBufferProblem(const BufferRange& range, const std::wstring& text, Lang::Language language);

// A path of the log file: not empty, and without the characters Windows does
// not allow in a name.
std::wstring PathProblem(const std::wstring& text, Lang::Language language);

}
