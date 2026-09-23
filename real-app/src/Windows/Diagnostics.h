#pragma once

#include "../Settings.h"

#include <Windows.h>

#include <mmdeviceapi.h>

#include <string>
#include <vector>

namespace miniant::Windows::Diagnostics {

// One audio endpoint as it is seen by the Windows audio engine.
struct EndpointInfo {
    std::wstring deviceName;
    std::wstring deviceId;
    std::wstring driverProvider;
    std::wstring driverVersion;

    bool isDefault = false;
    bool supportsAudioClient3 = false;

    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;

    uint32_t defaultPeriod = 0;
    uint32_t fundamentalPeriod = 0;
    uint32_t minPeriod = 0;
    uint32_t maxPeriod = 0;

    bool hasEnginePeriods = false;

    // True when the driver offers a period smaller than the default one, i.e.
    // when the latency reduction can have any effect for this endpoint.
    bool lowLatencyPossible = false;

    // Set when the endpoint could not be inspected.
    std::string error;
};

struct DeviceReport {
    bool ok = false;
    std::string error;

    std::wstring deviceName;
    std::wstring deviceId;
    std::wstring driverProvider;
    std::wstring driverVersion;

    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;

    uint32_t defaultPeriod = 0;
    uint32_t fundamentalPeriod = 0;
    uint32_t minPeriod = 0;
    uint32_t maxPeriod = 0;
    uint32_t currentPeriod = 0;

    bool supportsAudioClient3 = false;
    bool lowLatencyNotAvailable = false;
};

std::string GetWindowsVersion();

// Text report about the system, the audio endpoints and the current settings.
// Written to a file so that a user can send one file instead of screenshots.
std::wstring BuildReport(const Config::Settings& settings, const std::wstring& settingsPath);

// All active endpoints of the given flow, with the periods they support.
std::vector<EndpointInfo> EnumerateEndpoints(EDataFlow dataFlow, ::IMMDeviceEnumerator& enumerator);

}
