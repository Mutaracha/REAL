#include "SettingsChecks.h"

#include "../Lang.h"
#include "../Text.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cwchar>

using namespace miniant;
using namespace miniant::Windows;

namespace {

std::wstring AllowedRangeText(int minimum, int maximum) {
    return Text::ToWide(fmt::format(Lang::Utf8(Lang::Str::SettingsAllowedRange), minimum, maximum));
}

}

bool SettingsChecks::ParseNumber(const std::wstring& text, long& value) {
    if (text.empty() || text.size() > 9) {
        return false;
    }

    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return false;
        }
    }

    value = std::wcstol(text.c_str(), nullptr, 10);
    return true;
}

std::wstring SettingsChecks::NumberProblem(const std::wstring& text, const Config::NumberLimits& limits) {
    long value = 0;
    const bool fits = ParseNumber(text, value) && value >= limits.minimum && value <= limits.maximum;

    return fits ? std::wstring() : AllowedRangeText(limits.minimum, limits.maximum);
}

uint32_t SettingsChecks::BufferStep(const BufferRange& range) {
    return range.step > 0 ? range.step : 1;
}

std::wstring SettingsChecks::FixedBufferProblem(const BufferRange& range, const std::wstring& text) {
    const int maximum = Config::FIXED_BUFFER_FRAMES_LIMITS.maximum;

    long frames = 0;
    const bool number = ParseNumber(text, frames) && frames >= 1 && frames <= maximum;

    if (range.minimum == 0) {
        return number ? std::wstring() : AllowedRangeText(1, maximum);
    }

    const uint32_t step = BufferStep(range);
    const std::string device = Text::ToUtf8(
        range.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : range.deviceName);

    if (!number) {
        return Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsBufferAccepted), device, range.minimum, range.maximum, step));
    }

    const uint32_t value = static_cast<uint32_t>(frames);

    if (value < range.minimum || value > range.maximum) {
        const uint32_t nearest = value < range.minimum ? range.minimum : range.maximum;

        return Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsBufferOutOfRange), device, range.minimum, range.maximum, step, nearest));
    }

    if (value % step != 0) {
        const uint32_t lower = (std::max)(range.minimum, value - value % step);
        const uint32_t upper = (std::min)(range.maximum, lower + step);

        return Text::ToWide(fmt::format(
            Lang::Utf8(Lang::Str::SettingsBufferNotOnStep), device, range.minimum, range.maximum, step, lower, upper));
    }

    return {};
}

std::wstring SettingsChecks::PathProblem(const std::wstring& text) {
    bool blank = true;

    for (const wchar_t c : text) {
        if (c < L' ' || std::wcschr(L"<>\"|?*", c) != nullptr) {
            return Lang::Wide(Lang::Str::SettingsAllowedPath);
        }

        if (c != L' ') {
            blank = false;
        }
    }

    return blank ? Lang::Wide(Lang::Str::SettingsAllowedPath) : std::wstring();
}
