#include "TextMetrics.h"

#include "../Lang.h"

#include <algorithm>

namespace {

const int MINIMUM_BUTTON_WIDTH = 75;
// The space between the caption of a button and its border, on each side.
const int BUTTON_TEXT_MARGIN = 12;

// Every caption of a push button in the program: the main window and the
// settings window use one width, so a button looks the same everywhere.
const miniant::Lang::Str BUTTON_CAPTIONS[] = {
    miniant::Lang::Str::ButtonReinitialize,
    miniant::Lang::Str::ButtonExit,
    miniant::Lang::Str::SettingsOpenFile,
    miniant::Lang::Str::SettingsReload,
    miniant::Lang::Str::SettingsCancel,
    miniant::Lang::Str::SettingsSave,
};

// The screen is enough for measuring: the letter spacing of a font does not
// depend on the device the text is finally drawn on.
HDC GetMeasuringDC() {
    return ::GetDC(nullptr);
}

}

int miniant::Windows::MeasureTextWidth(HFONT font, const std::wstring& text) {
    if (text.empty()) {
        return 0;
    }

    HDC dc = GetMeasuringDC();
    if (dc == nullptr) {
        return 0;
    }

    const HGDIOBJ previous = font != nullptr ? ::SelectObject(dc, font) : nullptr;

    SIZE size = {};
    ::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);

    if (previous != nullptr) {
        ::SelectObject(dc, previous);
    }

    ::ReleaseDC(nullptr, dc);

    return size.cx;
}

int miniant::Windows::StandardButtonWidth(HFONT font, UINT dpi) {
    int widest = 0;

    for (const miniant::Lang::Str caption : BUTTON_CAPTIONS) {
        widest = (std::max)(widest, MeasureTextWidth(font, miniant::Lang::Wide(caption)));
    }

    // The width is measured in real pixels of the DPI, the layout is in design ones.
    const int design = dpi != 0 ? ::MulDiv(widest, 96, static_cast<int>(dpi)) : widest;

    return (std::max)(MINIMUM_BUTTON_WIDTH, design + 2 * BUTTON_TEXT_MARGIN);
}
