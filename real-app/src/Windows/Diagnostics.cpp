#include "Diagnostics.h"

#include "../AppVersion.h"
#include "../Log.h"
#include "../Text.h"
#include "ComPtr.h"
#include "Filesystem.h"
#include "WindowsError.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstddef>
#include <ctime>

using namespace miniant::Windows;
using namespace miniant::Windows::Diagnostics;

namespace {

// {a45c254e-df1c-4efd-8020-67d146a850e0}, 14 == PKEY_Device_FriendlyName
const PROPERTYKEY DEVICE_FRIENDLY_NAME_KEY = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    14
};

// Driver properties, see DEVPKEY_Device_Driver* in devpkey.h
const PROPERTYKEY DEVICE_DRIVER_PROVIDER_KEY = {
    { 0xa8b865dd, 0x2e3d, 0x4094, { 0xad, 0x97, 0xe5, 0x93, 0xa7, 0x0c, 0x75, 0xd6 } },
    9
};

const PROPERTYKEY DEVICE_DRIVER_VERSION_KEY = {
    { 0xa8b865dd, 0x2e3d, 0x4094, { 0xad, 0x97, 0xe5, 0x93, 0xa7, 0x0c, 0x75, 0xd6 } },
    3
};

std::wstring ReadStringProperty(IPropertyStore& store, const PROPERTYKEY& key) {
    PROPVARIANT value = {};
    if (FAILED(store.GetValue(key, &value))) {
        return {};
    }

    std::wstring result;
    if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        result = value.pwszVal;
    }

    ::PropVariantClear(&value);
    return result;
}

std::wstring GetDeviceId(IMMDevice& device) {
    LPWSTR id = nullptr;
    if (FAILED(device.GetId(&id)) || id == nullptr) {
        return {};
    }

    std::wstring result = id;
    ::CoTaskMemFree(id);
    return result;
}

void ReadFormat(const WAVEFORMATEX& format, uint16_t& channels, uint32_t& sampleRate, uint16_t& bitsPerSample) {
    channels = format.nChannels;
    sampleRate = format.nSamplesPerSec;
    bitsPerSample = format.wBitsPerSample;
}

uint32_t ToFrames(REFERENCE_TIME period, uint32_t sampleRate) {
    if (period <= 0 || sampleRate == 0) {
        return 0;
    }

    return static_cast<uint32_t>((period * static_cast<REFERENCE_TIME>(sampleRate)) / 10000000);
}

void ReadDeviceProperties(IMMDevice& device, EndpointInfo& info) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device.OpenPropertyStore(STGM_READ, store.GetAddressOf()))) {
        return;
    }

    info.deviceName = ReadStringProperty(*store.Get(), DEVICE_FRIENDLY_NAME_KEY);
    info.driverProvider = ReadStringProperty(*store.Get(), DEVICE_DRIVER_PROVIDER_KEY);
    info.driverVersion = ReadStringProperty(*store.Get(), DEVICE_DRIVER_VERSION_KEY);
}

void InspectEndpoint(IMMDevice& device, EndpointInfo& info) {
    ReadDeviceProperties(device, info);
    info.deviceId = GetDeviceId(device);

    ComPtr<IAudioClient3> audioClient3;
    const HRESULT v3Result = device.Activate(
        __uuidof(IAudioClient3),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient3.GetAddressOf()));

    info.supportsAudioClient3 = SUCCEEDED(v3Result);

    WAVEFORMATEX* format = nullptr;

    if (info.supportsAudioClient3) {
        if (FAILED(audioClient3->GetMixFormat(&format)) || format == nullptr) {
            info.error = "the mix format could not be read";
            return;
        }

        ReadFormat(*format, info.channels, info.sampleRate, info.bitsPerSample);

        const HRESULT hr = audioClient3->GetSharedModeEnginePeriod(
            format,
            &info.defaultPeriod,
            &info.fundamentalPeriod,
            &info.minPeriod,
            &info.maxPeriod);

        ::CoTaskMemFree(format);

        if (FAILED(hr)) {
            info.error = std::string("the engine periods could not be queried: ") + DescribeHResult(static_cast<long>(hr));
            return;
        }

        info.hasEnginePeriods = true;
        info.lowLatencyPossible = info.minPeriod < info.defaultPeriod;
        return;
    }

    // Without IAudioClient3 only the device period of the engine is available.
    ComPtr<IAudioClient> audioClient;
    const HRESULT v1Result = device.Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient.GetAddressOf()));

    if (FAILED(v1Result)) {
        info.error = std::string("the audio client could not be created: ") + DescribeHResult(static_cast<long>(v1Result));
        return;
    }

    if (SUCCEEDED(audioClient->GetMixFormat(&format)) && format != nullptr) {
        ReadFormat(*format, info.channels, info.sampleRate, info.bitsPerSample);
        ::CoTaskMemFree(format);
    }

    // IAudioClient::GetDevicePeriod() reports the periods in 100 ns units.
    REFERENCE_TIME defaultDevicePeriod = 0;
    REFERENCE_TIME minimumDevicePeriod = 0;

    if (SUCCEEDED(audioClient->GetDevicePeriod(&defaultDevicePeriod, &minimumDevicePeriod))) {
        info.defaultPeriod = ToFrames(defaultDevicePeriod, info.sampleRate);
        info.minPeriod = ToFrames(minimumDevicePeriod, info.sampleRate);
        info.fundamentalPeriod = info.minPeriod;
        info.maxPeriod = info.defaultPeriod;
        info.hasEnginePeriods = info.defaultPeriod != 0;
    }

    info.error = "the driver does not expose IAudioClient3, so small buffers are not available for this device "
                 "(typical for Bluetooth, HDMI/DisplayPort receivers and some virtual drivers)";
}

std::string FlowName(EDataFlow flow) {
    return flow == eRender ? "playback (render)" : "recording (capture)";
}

std::string Milliseconds(uint32_t frames, uint32_t sampleRate) {
    if (sampleRate == 0) {
        return "-";
    }

    return fmt::format("{:.2f} ms", 1000.0 * static_cast<double>(frames) / static_cast<double>(sampleRate));
}

std::string Periods(const EndpointInfo& info) {
    if (!info.hasEnginePeriods) {
        return "unknown";
    }

    if (!info.supportsAudioClient3) {
        return fmt::format(
            "device period {} frames ({})",
            info.defaultPeriod,
            Milliseconds(info.defaultPeriod, info.sampleRate));
    }

    return fmt::format(
        "default {} frames ({}), minimum {} frames ({}), fundamental {} frames, maximum {} frames",
        info.defaultPeriod,
        Milliseconds(info.defaultPeriod, info.sampleRate),
        info.minPeriod,
        Milliseconds(info.minPeriod, info.sampleRate),
        info.fundamentalPeriod,
        info.maxPeriod);
}

}

std::string miniant::Windows::Diagnostics::GetWindowsVersion() {
    // RtlGetVersion reports the real version, unlike GetVersionEx which lies
    // unless the executable is manifested for the newest Windows.
    struct OsVersionInfo {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    };

    using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);

    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr) {
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));

        if (rtlGetVersion != nullptr) {
            OsVersionInfo info = {};
            info.dwOSVersionInfoSize = sizeof(info);

            if (rtlGetVersion(&info) == 0) {
                std::string name = "Windows";
                if (info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000) {
                    name = "Windows 11";
                } else if (info.dwMajorVersion == 10) {
                    name = "Windows 10";
                } else if (info.dwMajorVersion == 6) {
                    name = info.dwMinorVersion == 3 ? "Windows 8.1" : "Windows 8";
                }

                std::string edition;
                if (info.szCSDVersion[0] != L'\0') {
                    edition = " " + Text::ToUtf8(info.szCSDVersion);
                }

                return fmt::format(
                    "{} {}.{}.{}{}",
                    name,
                    info.dwMajorVersion,
                    info.dwMinorVersion,
                    info.dwBuildNumber,
                    edition);
            }
        }
    }

    return "unknown";
}

std::vector<EndpointInfo> miniant::Windows::Diagnostics::EnumerateEndpoints(
    EDataFlow dataFlow,
    ::IMMDeviceEnumerator& enumerator) {
    std::vector<EndpointInfo> result;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator.EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) {
        return result;
    }

    ComPtr<IMMDevice> defaultDevice;
    std::wstring defaultDeviceId;
    if (SUCCEEDED(enumerator.GetDefaultAudioEndpoint(dataFlow, eConsole, defaultDevice.GetAddressOf())) &&
        defaultDevice) {
        defaultDeviceId = GetDeviceId(*defaultDevice.Get());
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return result;
    }

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf())) || !device) {
            continue;
        }

        EndpointInfo info;
        InspectEndpoint(*device.Get(), info);
        info.isDefault = !defaultDeviceId.empty() && info.deviceId == defaultDeviceId;
        result.push_back(std::move(info));
    }

    return result;
}

std::wstring miniant::Windows::Diagnostics::BuildReport(
    const Config::Settings& settings,
    const std::wstring& settingsPath) {
    std::string text;

    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    ::localtime_s(&local, &now);

    char timestamp[64] = {};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

    text += "REAL diagnostics\n";
    text += "================\n\n";
    text += fmt::format("Version:    {} ({})\n", AppInfo::VERSION.ToString(), Text::ToUtf8(AppInfo::DESCRIPTION));
    text += fmt::format("Generated:  {}\n", timestamp);
    text += fmt::format("Windows:    {}\n", GetWindowsVersion());
    text += fmt::format("Executable: {}\n", Text::ToUtf8(Filesystem::GetExecutablePath()));
    text += fmt::format("Settings:   {}\n", Text::ToUtf8(settingsPath));
    text += fmt::format("Config:     {}\n", Config::Describe(settings));
    text += "\n";

    Log::Info("Diagnostics: location and configuration collected.");
    Log::Flush();

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        // Already initialised in another mode by the caller: keep going.
        hr = S_OK;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (SUCCEEDED(hr)) {
        hr = ::CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(enumerator.GetAddressOf()));
    }

    Log::Info("Diagnostics: the device enumerator is {}.", enumerator ? "ready" : "not available");
    Log::Flush();

    if (!enumerator) {
        text += fmt::format(
            "ERROR: the audio device enumerator could not be created ({}).\n",
            DescribeHResult(static_cast<long>(hr)));
        text += "The Windows audio service (Audiosrv) is probably not running.\n";
    } else {
        const EDataFlow flows[] = { eRender, eCapture };
        for (EDataFlow flow : flows) {
            text += fmt::format("--- {} devices ---\n\n", FlowName(flow));

            const std::vector<EndpointInfo> endpoints = EnumerateEndpoints(flow, *enumerator.Get());
            Log::Info("Diagnostics: {} endpoints for flow {}.", endpoints.size(), FlowName(flow));
            Log::Flush();

            if (endpoints.empty()) {
                text += "No active devices.\n\n";
                continue;
            }

            for (const EndpointInfo& info : endpoints) {
                text += fmt::format(
                    "{}{}\n",
                    Text::ToUtf8(info.deviceName.empty() ? std::wstring(L"<unknown device>") : info.deviceName),
                    info.isDefault ? "   [default]" : "");
                text += fmt::format("    id:       {}\n", Text::ToUtf8(info.deviceId));

                if (!info.driverProvider.empty() || !info.driverVersion.empty()) {
                    text += fmt::format(
                        "    driver:   {} {}\n",
                        Text::ToUtf8(info.driverProvider),
                        Text::ToUtf8(info.driverVersion));
                }

                if (info.sampleRate != 0) {
                    text += fmt::format(
                        "    format:   {} Hz, {} channels, {} bit\n",
                        info.sampleRate,
                        info.channels,
                        info.bitsPerSample);
                }

                text += fmt::format(
                    "    periods:  {}\n",
                    info.hasEnginePeriods ? Periods(info) : "unknown");

                if (info.supportsAudioClient3) {
                    text += fmt::format(
                        "    result:   {} (IAudioClient3 is supported)\n",
                        info.lowLatencyPossible
                            ? fmt::format(
                                  "small buffers are available, a buffer of {} can be requested",
                                  Milliseconds(info.minPeriod, info.sampleRate))
                            : "the driver offers nothing smaller than its default buffer, so REAL cannot lower the latency here");
                } else {
                    text += fmt::format("    result:   {}\n", info.error.empty() ? "IAudioClient3 is not available" : info.error);
                }

                text += "\n";
            }
        }

        // What REAL itself is doing right now.
        text += "--- summary ---\n\n";
        text += "A device is suitable for the latency reduction when its minimum period is\n";
        text += "smaller than its default period (see 'result' above). Typical exceptions:\n";
        text += "Bluetooth endpoints (10 ms by design), HDMI/DisplayPort receivers and some\n";
        text += "vendor drivers (Realtek, Nahimic, ACX) as well as virtual devices.\n";
        text += "\nIf a suitable device is used by default right after the next start, the status\n";
        text += "line of the window shows the buffer size the audio engine is running with,\n";
        text += "for example '2.67 ms - Speakers (Realtek Audio)'.\n";
    }

    if (comInitialized) {
        ::CoUninitialize();
    }

    Log::Info("Diagnostics: the report text has been built.");
    Log::Flush();

    return Text::ToWide(text);
}
