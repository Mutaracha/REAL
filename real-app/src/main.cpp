#include "App.h"
#include "Log.h"

#include <Windows.h>

#include <exception>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, PWSTR /*commandLine*/, int /*showCommand*/) {
    try {
        miniant::App app(instance);
        return app.Run();
    } catch (const std::exception& error) {
        // Without this an uncaught exception (for example a broken format
        // string) only shows up as a silent abort() with the code 0xC0000409.
        miniant::Log::Error("Unhandled exception: {}. REAL will exit.", error.what());
        miniant::Log::Flush();
        miniant::Log::Shutdown();
        return 3;
    } catch (...) {
        miniant::Log::Error("Unhandled exception of an unknown type. REAL will exit.");
        miniant::Log::Flush();
        miniant::Log::Shutdown();
        return 3;
    }
}
