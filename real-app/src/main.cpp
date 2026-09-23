#include "App.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, PWSTR /*commandLine*/, int /*showCommand*/) {
    miniant::App app(instance);
    return app.Run();
}
