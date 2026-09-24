#include "Diagnostics.h"

#include "../AppVersion.h"
#include "../Lang.h"
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
#include <cstdio>
#include <ctime>

using namespace miniant::Windows;
using namespace miniant::Windows::Diagnostics;
using namespace miniant::Lang;

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
            info.error = Lang::Utf8(Lang::Str::DiagMixFormatFailed);
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
            info.error = fmt::format(Lang::Utf8(Lang::Str::DiagEnginePeriodsFailed), DescribeHResult(static_cast<long>(hr)));
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
        info.error = fmt::format(Lang::Utf8(Lang::Str::DiagActivateFailed), DescribeHResult(static_cast<long>(v1Result)));
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

    info.error = Lang::Utf8(Lang::Str::DiagNoAudioClient3Detail);
}

std::string FlowName(EDataFlow flow) {
    return Lang::Utf8(flow == eRender ? Lang::Str::DiagFlowRender : Lang::Str::DiagFlowCapture);
}

std::string Milliseconds(uint32_t frames, uint32_t sampleRate) {
    if (sampleRate == 0) {
        return "-";
    }

    return fmt::format("{:.2f} ms", 1000.0 * static_cast<double>(frames) / static_cast<double>(sampleRate));
}

std::string Periods(const EndpointInfo& info) {
    if (!info.hasEnginePeriods) {
        return Lang::Utf8(Lang::Str::DiagUnknown);
    }

    if (!info.supportsAudioClient3) {
        return fmt::format(
            Lang::Utf8(Lang::Str::DiagPeriodsNoClient3),
            info.defaultPeriod,
            Milliseconds(info.defaultPeriod, info.sampleRate));
    }

    return fmt::format(
        Lang::Utf8(Lang::Str::DiagPeriodsDetail),
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

std::string miniant::Windows::Diagnostics::BuildReport(
    const Config::Settings& settings,
    const std::wstring& settingsPath) {
    std::string text;

    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    ::localtime_s(&local, &now);

    char timestamp[64] = {};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

    text += Lang::Utf8(Lang::Str::DiagTitle);
    text += "\n";
    text += Lang::Utf8(Lang::Str::DiagRule);
    text += "\n\n";
    text += fmt::format(
        Lang::Utf8(Lang::Str::DiagVersion), AppInfo::VERSION.ToString(), Text::ToUtf8(AppInfo::DESCRIPTION));
    text += fmt::format(Lang::Utf8(Lang::Str::DiagGenerated), timestamp);
    text += fmt::format(Lang::Utf8(Lang::Str::DiagWindows), GetWindowsVersion());
    text += fmt::format(Lang::Utf8(Lang::Str::DiagExecutable), Text::ToUtf8(Filesystem::GetExecutablePath()));
    text += fmt::format(Lang::Utf8(Lang::Str::DiagSettings), Text::ToUtf8(settingsPath));
    text += fmt::format(Lang::Utf8(Lang::Str::DiagConfig), Config::Describe(settings));
    text += "\n";

    Log::Info(Lang::Utf8(Lang::Str::LogDiagCollected));
    Log::Flush();

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        // Already initialised in another mode by the caller: keep going.
        hr = S_OK;
    }

    // Every COM interface stays inside this scope: the interfaces must be
    // released before CoUninitialize() is called, otherwise the last Release()
    // would run on an object whose server has already been unloaded.
    {
        ComPtr<IMMDeviceEnumerator> enumerator;

        if (SUCCEEDED(hr)) {
            hr = ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(enumerator.GetAddressOf()));
        }

        Log::Info(Lang::Utf8(enumerator ? Lang::Str::LogDiagEnumeratorReady : Lang::Str::LogDiagEnumeratorMissing));
        Log::Flush();

        if (!enumerator) {
            text += fmt::format(
                Lang::Utf8(Lang::Str::DiagEnumeratorError), DescribeHResult(static_cast<long>(hr)));
            text += Lang::Utf8(Lang::Str::DiagAudiosrvHint);
        } else {
            const EDataFlow flows[] = { eRender, eCapture };
            for (EDataFlow flow : flows) {
                text += fmt::format(Lang::Utf8(Lang::Str::DiagFlowHeader), FlowName(flow));

                const std::vector<EndpointInfo> endpoints = EnumerateEndpoints(flow, *enumerator.Get());
                Log::Info(
                    Lang::Utf8(Lang::Str::LogDiagEndpoints),
                    endpoints.size(),
                    Lang::Utf8(flow == EDataFlow::eRender ? Lang::Str::FlowRender : Lang::Str::FlowCapture));
                Log::Flush();

                if (endpoints.empty()) {
                    text += Lang::Utf8(Lang::Str::DiagNoDevices);
                    continue;
                }

                for (const EndpointInfo& info : endpoints) {
                    text += fmt::format(
                        "{}{}\n",
                        Text::ToUtf8(info.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : info.deviceName),
                        info.isDefault ? Lang::Utf8(Lang::Str::DiagDefaultMark) : "");
                    text += fmt::format(Lang::Utf8(Lang::Str::DiagDeviceId), Text::ToUtf8(info.deviceId));

                    if (!info.driverProvider.empty() || !info.driverVersion.empty()) {
                        text += fmt::format(
                            Lang::Utf8(Lang::Str::DiagDriver),
                            Text::ToUtf8(info.driverProvider),
                            Text::ToUtf8(info.driverVersion));
                    }

                    if (info.sampleRate != 0) {
                        text += fmt::format(
                            Lang::Utf8(Lang::Str::DiagFormat),
                            info.sampleRate,
                            info.channels,
                            info.bitsPerSample);
                    }

                    text += fmt::format(
                        Lang::Utf8(Lang::Str::DiagPeriods),
                        info.hasEnginePeriods ? Periods(info) : Lang::Utf8(Lang::Str::DiagUnknown));

                    if (info.supportsAudioClient3) {
                        text += fmt::format(
                            Lang::Utf8(Lang::Str::DiagResult),
                            info.lowLatencyPossible
                                ? fmt::format(
                                      Lang::Utf8(Lang::Str::DiagSmallBuffer),
                                      Milliseconds(info.minPeriod, info.sampleRate))
                                : std::string(Lang::Utf8(Lang::Str::DiagNoGain)));
                    } else {
                        text += fmt::format(
                            Lang::Utf8(Lang::Str::DiagResult),
                            info.error.empty() ? Lang::Utf8(Lang::Str::DiagNoAudioClient3) : info.error);
                    }

                    text += "\n";
                }
            }

            // What REAL itself is doing right now.
            text += Lang::Utf8(Lang::Str::DiagSummaryHeader);
            text += Lang::Utf8(Lang::Str::DiagSummary);
        }
    }

    // The COM interfaces have been released by now (see the scope above).
    if (comInitialized) {
        ::CoUninitialize();
    }

    Log::Info(Lang::Utf8(Lang::Str::LogDiagReportBuilt), text.size());
    Log::Flush();

    return text;
}
