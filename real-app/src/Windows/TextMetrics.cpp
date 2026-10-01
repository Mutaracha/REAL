#include "TextMetrics.h"

namespace {

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
