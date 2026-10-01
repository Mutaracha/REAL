#pragma once

#include <Windows.h>

#include <string>

namespace miniant::Windows {

// The width of a line of text in the given font. The windows use it to size a
// control by its own caption: a check box or a link that is as wide as the
// whole window reacts to clicks far away from what the user sees.
int MeasureTextWidth(HFONT font, const std::wstring& text);

}
