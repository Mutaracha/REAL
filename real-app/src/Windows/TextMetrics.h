#pragma once

#include <Windows.h>

#include <string>

namespace miniant::Windows {

// The width of a line of text in the given font. The windows use it to size a
// control by its own caption: a check box or a link that is as wide as the
// whole window reacts to clicks far away from what the user sees.
int MeasureTextWidth(HFONT font, const std::wstring& text);

// The push buttons of the settings window share one size: the height of a
// button in a dialog box of Windows, and the width of the longest caption of
// all of them in either language with its margins, never narrower than the
// standard 75 pixels.
// The values are design pixels (96 DPI); the font is the interface font of
// the given DPI.
constexpr int STANDARD_BUTTON_HEIGHT = 23;
int StandardButtonWidth(HFONT font, UINT dpi);

}
