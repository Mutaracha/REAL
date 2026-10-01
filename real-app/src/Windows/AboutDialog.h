#pragma once

#include <Windows.h>

namespace miniant::Windows {

// Small "about" window: the version, one sentence about what the program does
// and a clickable link to the project. It runs its own message loop and returns
// when the user closes it.
void ShowAboutDialog(HWND owner, HINSTANCE instance);

}
