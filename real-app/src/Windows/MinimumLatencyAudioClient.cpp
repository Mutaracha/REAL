#include "MinimumLatencyAudioClient.h"

#include "../Text.h"
#include "../Lang.h"
#include "ComPtr.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstddef>

using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

#ifndef AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
#define AUDCLNT_E_ENGINE_PERIODICITY_LOCKED _HRESULT_TYPEDEF_(0x88890028L)
#endif

#ifndef AUDCLNT_E_ENGINE_FORMAT_LOCKED
#define AUDCLNT_E_ENGINE_FORMAT_LOCKED _HRESULT_TYPEDEF_(0x88890029L)
#endif

#ifndef AUDCLNT_E_DEVICE_INVALIDATED
#define AUDCLNT_E_DEVICE_INVALIDATED _HRESULT_TYPEDEF_(0x88890004L)
#endif

#ifndef AUDCLNT_E_SERVICE_NOT_RUNNING
#define AUDCLNT_E_SERVICE_NOT_RUNNING _HRESULT_TYPEDEF_(0x88890010L)
#endif

#ifndef AUDCLNT_E_RESOURCES_INVALIDATED
#define AUDCLNT_E_RESOURCES_INVALIDATED _HRESULT_TYPEDEF_(0x88890026L)
#endif

namespace {
// The endpoint or the audio service was not ready when we asked: this happens
// while a device is being enabled/disabled and is worth another attempt.
bool IsTransientEndpointError(long code) {
    if (code == static_cast<long>(HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) ||
        code == static_cast<long>(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) ||
        code == static_cast<long>(HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))) {
        return true;
    }

    return code == static_cast<long>(AUDCLNT_E_DEVICE_INVALIDATED) ||
        code == static_cast<long>(AUDCLNT_E_SERVICE_NOT_RUNNING) ||
        code == static_cast<long>(AUDCLNT_E_RESOURCES_INVALIDATED);
}

std::string DescribeEndpointError(Lang::Str what, long code) {
    const std::string prefix = Lang::Utf8(what);

    if (IsTransientEndpointError(code)) {
        return prefix + Lang::Utf8(Lang::Str::ErrEndpointTransient);
    }

    return prefix + ": " + DescribeHResult(code);
}
}

namespace {

// {a45c254e-df1c-4efd-8020-67d146a850e0}, 14 == PKEY_Device_FriendlyName
const PROPERTYKEY DEVICE_FRIENDLY_NAME_KEY = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    14
};

std::wstring GetDeviceFriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf()))) {
        return {};
    }

    PROPVARIANT value = {};
    if (FAILED(store->GetValue(DEVICE_FRIENDLY_NAME_KEY, &value))) {
        return {};
    }

    std::wstring name;
    if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }

    ::PropVariantClear(&value);
    return name;
}

uint32_t ChoosePeriod(
    PeriodSelection selection,
    uint32_t requestedPeriodFrames,
    const AudioStreamInfo& info) {
    uint32_t desired = info.minPeriod;

    if (selection == PeriodSelection::Fundamental) {
        desired = info.fundamentalPeriod;
    } else if (selection == PeriodSelection::Fixed) {
        desired = requestedPeriodFrames;
    }

    if (info.fundamentalPeriod > 0) {
        const uint32_t steps = desired / info.fundamentalPeriod;
        desired = steps > 0 ? steps * info.fundamentalPeriod : info.fundamentalPeriod;
    }

    desired = std::max(desired, info.minPeriod);
    desired = std::min(desired, info.maxPeriod);
    return desired;
}

}

double AudioStreamInfo::PeriodMilliseconds(uint32_t frames) const {
    if (sampleRate == 0) {
        return 0.0;
    }

    return 1000.0 * static_cast<double>(frames) / static_cast<double>(sampleRate);
}

MinimumLatencyAudioClient::MinimumLatencyAudioClient(MinimumLatencyAudioClient&& other) noexcept:
    m_audioClient(other.m_audioClient),
    m_format(other.m_format),
    m_info(std::move(other.m_info)) {
    other.m_audioClient = nullptr;
    other.m_format = nullptr;
}

MinimumLatencyAudioClient& MinimumLatencyAudioClient::operator=(MinimumLatencyAudioClient&& rhs) noexcept {
    if (this != &rhs) {
        Stop();

        m_audioClient = rhs.m_audioClient;
        m_format = rhs.m_format;
        m_info = std::move(rhs.m_info);

        rhs.m_audioClient = nullptr;
        rhs.m_format = nullptr;
    }

    return *this;
}

MinimumLatencyAudioClient::~MinimumLatencyAudioClient() {
    Stop();
}

void MinimumLatencyAudioClient::Stop() {
    if (m_audioClient != nullptr) {
        m_audioClient->Stop();
        m_audioClient->Release();
        m_audioClient = nullptr;
    }

    if (m_format != nullptr) {
        ::CoTaskMemFree(m_format);
        m_format = nullptr;
    }
}

bool MinimumLatencyAudioClient::IsActive() const {
    return m_audioClient != nullptr;
}

const AudioStreamInfo& MinimumLatencyAudioClient::GetInfo() const {
    return m_info;
}

tl::expected<uint32_t, WindowsError> MinimumLatencyAudioClient::GetCurrentPeriod() {
    if (m_audioClient == nullptr) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrStreamNotRunning)));
    }

    WAVEFORMATEX* currentFormat = nullptr;
    uint32_t currentPeriod = 0;

    const HRESULT hr = m_audioClient->GetCurrentSharedModeEnginePeriod(&currentFormat, &currentPeriod);
    if (currentFormat != nullptr) {
        ::CoTaskMemFree(currentFormat);
    }

    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError(static_cast<long>(hr)));
    }

    m_info.currentPeriod = currentPeriod;
    return currentPeriod;
}

tl::expected<MinimumLatencyAudioClient, WindowsError> MinimumLatencyAudioClient::Start(
    IMMDeviceEnumerator& enumerator,
    EDataFlow dataFlow,
    ERole role,
    PeriodSelection selection,
    uint32_t requestedPeriodFrames,
    bool allowPeriodSnap) {
    ComPtr<IMMDevice> device;
    HRESULT hr = enumerator.GetDefaultAudioEndpoint(dataFlow, role, device.GetAddressOf());
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError(
            DescribeEndpointError(Lang::Str::ErrOpenEndpoint, static_cast<long>(hr))));
    }

    ComPtr<IAudioClient3> audioClient;
    hr = device->Activate(
        __uuidof(IAudioClient3),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (hr == E_NOINTERFACE) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrNoAudioClient3)));
    }

    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError(
            fmt::format(Lang::Utf8(Lang::Str::ErrActivateClient), DescribeHResult(static_cast<long>(hr)))));
    }

    WAVEFORMATEX* format = nullptr;
    hr = audioClient->GetMixFormat(&format);
    if (FAILED(hr) || format == nullptr) {
        return tl::make_unexpected(WindowsError(
            fmt::format(Lang::Utf8(Lang::Str::ErrMixFormat), DescribeHResult(static_cast<long>(hr)))));
    }

    AudioStreamInfo info;
    info.dataFlow = dataFlow;
    info.role = role;
    info.deviceName = GetDeviceFriendlyName(device.Get());
    info.sampleRate = format->nSamplesPerSec;
    info.channels = format->nChannels;
    info.bitsPerSample = format->wBitsPerSample;

    LPWSTR deviceId = nullptr;
    if (SUCCEEDED(device->GetId(&deviceId)) && deviceId != nullptr) {
        info.deviceId = deviceId;
        ::CoTaskMemFree(deviceId);
    }

    hr = audioClient->GetSharedModeEnginePeriod(
        format,
        &info.defaultPeriod,
        &info.fundamentalPeriod,
        &info.minPeriod,
        &info.maxPeriod);
    if (FAILED(hr)) {
        ::CoTaskMemFree(format);
        return tl::make_unexpected(WindowsError(
            fmt::format(Lang::Utf8(Lang::Str::ErrEnginePeriods), DescribeHResult(static_cast<long>(hr)))));
    }

    info.lowLatencyNotAvailable = info.minPeriod >= info.defaultPeriod;

    const uint32_t period = ChoosePeriod(selection, requestedPeriodFrames, info);
    info.requestedPeriod = period;

    // Nothing to gain for this device: the smallest period its driver offers is
    // the one the engine uses anyway. Holding a stream would not change the
    // buffer size, but on Windows 11 it would keep audio resources (and a CPU
    // thread) reserved, so no stream is created at all.
    if (info.lowLatencyNotAvailable) {
        info.currentPeriod = info.defaultPeriod;
        ::CoTaskMemFree(format);

        MinimumLatencyAudioClient result;
        result.m_info = std::move(info);
        return std::move(result);
    }

    hr = audioClient->InitializeSharedAudioStream(0, period, format, nullptr);
    if (hr == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED && allowPeriodSnap) {
        WAVEFORMATEX* currentFormat = nullptr;
        uint32_t currentPeriod = 0;

        if (SUCCEEDED(audioClient->GetCurrentSharedModeEnginePeriod(&currentFormat, &currentPeriod)) && currentPeriod > 0) {
            if (currentFormat != nullptr) {
                ::CoTaskMemFree(currentFormat);
            }

            info.requestedPeriod = currentPeriod;
            info.acceptedLockedPeriod = true;

            hr = audioClient->InitializeSharedAudioStream(0, currentPeriod, format, nullptr);
        }
    }

    if (FAILED(hr)) {
        ::CoTaskMemFree(format);

        if (hr == AUDCLNT_E_ENGINE_FORMAT_LOCKED || hr == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED) {
            return tl::make_unexpected(WindowsError(
                fmt::format(Lang::Utf8(Lang::Str::ErrEngineLocked), DescribeHResult(static_cast<long>(hr)))));
        }

        return tl::make_unexpected(WindowsError(
            fmt::format(Lang::Utf8(Lang::Str::ErrInitStream), DescribeHResult(static_cast<long>(hr)))));
    }

    hr = audioClient->Start();
    if (FAILED(hr)) {
        ::CoTaskMemFree(format);
        return tl::make_unexpected(WindowsError(
            fmt::format(Lang::Utf8(Lang::Str::ErrStartStream), DescribeHResult(static_cast<long>(hr)))));
    }

    WAVEFORMATEX* currentFormat = nullptr;
    if (SUCCEEDED(audioClient->GetCurrentSharedModeEnginePeriod(&currentFormat, &info.currentPeriod))) {
        if (currentFormat != nullptr) {
            ::CoTaskMemFree(currentFormat);
        }
    } else {
        info.currentPeriod = period;
    }

    MinimumLatencyAudioClient result;
    result.m_audioClient = audioClient.Detach();
    result.m_format = format;
    result.m_info = std::move(info);
    return std::move(result);
}

std::string miniant::Windows::WasapiLatency::DescribeStreamWin32(const AudioStreamInfo& info) {
    return fmt::format(
        "{}, {}, {} Hz, {} ch, {} bit, period {} frames ({:.2f} ms)",
        Text::ToUtf8(info.deviceName.empty() ? std::wstring(L"<unknown device>") : info.deviceName),
        info.dataFlow == eRender ? "render" : "capture",
        info.sampleRate,
        info.channels,
        info.bitsPerSample,
        info.currentPeriod,
        info.PeriodMilliseconds(info.currentPeriod));
}
