#include "App.h"
#include "Log.h"

#include "Lang.h"

#include <Windows.h>

#include <exception>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, PWSTR /*commandLine*/, int /*showCommand*/) {
    try {
        miniant::App app(instance);
        return app.Run();
    } catch (const std::exception& error) {
        // Without this an uncaught exception (for example a broken format
        // string) only shows up as a silent abort() with the code 0xC0000409.
        miniant::Log::Error(
            miniant::Lang::Utf8(miniant::Lang::Str::ErrUnhandledException), error.what());
        miniant::Log::Flush();
        miniant::Log::Shutdown();
        return 3;
    } catch (...) {
        miniant::Log::Error(miniant::Lang::Utf8(miniant::Lang::Str::ErrUnhandledExceptionUnknown));
        miniant::Log::Flush();
        miniant::Log::Shutdown();
        return 3;
    }
}
