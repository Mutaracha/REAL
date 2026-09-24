#include "AudioSession.h"

#include "Lang.h"
#include "Log.h"
#include "Text.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cwctype>
#include <cstring>

using namespace miniant::Audio;
using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

namespace {

const CLSID CLSID_MMDeviceEnumeratorLocal = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumeratorLocal = __uuidof(IMMDeviceEnumerator);

EDataFlow ToDataFlow(miniant::Config::DataFlow flow) {
    switch (flow) {
        case miniant::Config::DataFlow::Capture: return eCapture;
        case miniant::Config::DataFlow::Both: return eRender;
        default: return eRender;
    }
}

ERole ToRole(miniant::Config::DeviceRole role) {
    switch (role) {
        case miniant::Config::DeviceRole::Multimedia: return eMultimedia;
        case miniant::Config::DeviceRole::Communications: return eCommunications;
        default: return eConsole;
    }
}

PeriodSelection ToPeriodSelection(miniant::Config::PeriodSelection selection) {
    switch (selection) {
        case miniant::Config::PeriodSelection::Fundamental: return PeriodSelection::Fundamental;
        case miniant::Config::PeriodSelection::Fixed: return PeriodSelection::Fixed;
        default: return PeriodSelection::Minimum;
    }
}

std::vector<EDataFlow> GetDataFlows(miniant::Config::DataFlow flow) {
    if (flow == miniant::Config::DataFlow::Both) {
        return { eRender, eCapture };
    }

    return { ToDataFlow(flow) };
}

std::wstring GetDeviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)) || id == nullptr) {
        return {};
    }

    std::wstring result(id);
    ::CoTaskMemFree(id);
    return result;
}

bool SameDevice(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) {
        return true;
    }

    if (left.size() != right.size()) {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) {
            return false;
        }
    }

    return true;
}

}

AudioSession::AudioSession() = default;

AudioSession::~AudioSession() {
    Shutdown();
}

tl::expected<void, WindowsError> AudioSession::Initialize(DeviceEventHandler handler) {
    if (m_enumerator) {
        return {};
    }

    HRESULT hr = ::CoCreateInstance(
        CLSID_MMDeviceEnumeratorLocal,
        nullptr,
        CLSCTX_ALL,
        IID_IMMDeviceEnumeratorLocal,
        reinterpret_cast<void**>(m_enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError(fmt::format(
            Lang::Utf8(Lang::Str::ErrAudioEnumeratorFailed), DescribeHResult(static_cast<long>(hr)))));
    }

    m_notification = std::make_unique<DeviceNotificationClient>(std::move(handler));

    hr = m_enumerator->RegisterEndpointNotificationCallback(m_notification.get());
    if (FAILED(hr)) {
        m_notification.reset();
        return tl::make_unexpected(WindowsError(fmt::format(
            Lang::Utf8(Lang::Str::ErrAudioNotificationsFailed), DescribeHResult(static_cast<long>(hr)))));
    }

    m_notificationsRegistered = true;
    return {};
}

void AudioSession::Shutdown() {
    Stop();

    if (m_notificationsRegistered && m_enumerator && m_notification) {
        m_enumerator->UnregisterEndpointNotificationCallback(m_notification.get());
        m_notificationsRegistered = false;
    }

    m_notification.reset();
    m_enumerator.Reset();
}

void AudioSession::Stop() {
    m_streams.clear();
    m_streamsInfo.clear();
}

bool AudioSession::IsActive() const {
    return !m_streams.empty();
}

const std::vector<AudioStreamInfo>& AudioSession::GetStreams() const {
    return m_streamsInfo;
}

IMMDeviceEnumerator* AudioSession::GetEnumerator() const {
    return m_enumerator.Get();
}

tl::expected<void, WindowsError> AudioSession::Apply(const miniant::Config::Settings& settings) {
    if (!m_enumerator) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrNotInitialised)));
    }

    Stop();

    std::vector<std::string> errors;
    const std::vector<EDataFlow> flows = GetDataFlows(settings.audio.dataFlow);

    for (EDataFlow flow : flows) {
        auto stream = MinimumLatencyAudioClient::Start(
            *m_enumerator.Get(),
            flow,
            ToRole(settings.audio.role),
            ToPeriodSelection(settings.audio.periodSelection),
            settings.audio.requestedPeriodFrames,
            settings.audio.allowPeriodSnap);

        if (!stream) {
            const std::string message =
                std::string(Lang::Utf8(flow == EDataFlow::eRender ? Lang::Str::FlowRender : Lang::Str::FlowCapture))
                + ": " + stream.error().GetMessage();
            errors.push_back(message);
            Log::Error(Lang::Utf8(Lang::Str::LogLowLatencyFailed), message);
            continue;
        }

        const AudioStreamInfo& info = stream->GetInfo();

        if (!stream->IsActive()) {
            // The driver has nothing smaller than the default buffer, so no
            // stream is held (see MinimumLatencyAudioClient::Start).
            m_streamsInfo.push_back(info);
            Log::Warn(
                Lang::Utf8(Lang::Str::LogDriverMinimum),
                Text::ToUtf8(info.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : info.deviceName),
                info.defaultPeriod,
                info.PeriodMilliseconds(info.defaultPeriod));
            continue;
        }

        Log::Info(Lang::Utf8(Lang::Str::LogLowLatencyStarted), DescribeStreamWin32(info));

        if (info.acceptedLockedPeriod) {
            Log::Info(Lang::Utf8(Lang::Str::LogPeriodLocked), info.requestedPeriod);
        }

        m_streamsInfo.push_back(info);
        m_streams.push_back(std::move(*stream));
    }

    if (m_streams.empty() && !errors.empty()) {
        std::string message = Lang::Utf8(Lang::Str::ErrLowLatency);
        message += " ";
        message += errors.front();

        return tl::make_unexpected(WindowsError(message));
    }

    if (m_streams.empty() && m_streamsInfo.empty()) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrNoEndpoint)));
    }

    return {};
}

std::wstring AudioSession::GetStatusText() const {
    if (m_streamsInfo.empty()) {
        return Lang::Wide(Lang::Str::StatusNotActive);
    }

    const AudioStreamInfo& first = m_streamsInfo.front();

    if (m_streams.empty()) {
        // Every inspected device already uses its smallest buffer.
        std::string text = fmt::format(
            Lang::Utf8(Lang::Str::StatusDriverMinimum),
            first.PeriodMilliseconds(first.currentPeriod),
            Text::ToUtf8(first.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : first.deviceName));

        if (m_streamsInfo.size() > 1) {
            text += fmt::format(Lang::Utf8(Lang::Str::StatusMoreDevices), m_streamsInfo.size() - 1);
        }

        return Text::ToWide(text);
    }

    std::string text = fmt::format(
        Lang::Utf8(Lang::Str::StatusActive),
        first.PeriodMilliseconds(first.currentPeriod),
        Text::ToUtf8(first.deviceName.empty() ? Lang::Wide(Lang::Str::UnknownDevice) : first.deviceName));

    if (first.acceptedLockedPeriod) {
        text += Lang::Utf8(Lang::Str::StatusPeriodLocked);
    }

    if (m_streamsInfo.size() > 1) {
        text += fmt::format(Lang::Utf8(Lang::Str::StatusMoreDevices), m_streamsInfo.size() - 1);
    }

    return Text::ToWide(text);
}

tl::expected<void, WindowsError> AudioSession::Validate() {
    if (!m_enumerator) {
        return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrNotInitialised)));
    }

    if (m_streams.empty()) {
        return {};
    }

    for (size_t i = 0; i < m_streams.size(); ++i) {
        auto period = m_streams[i].GetCurrentPeriod();
        if (!period) {
            return tl::make_unexpected(WindowsError(fmt::format(
                Lang::Utf8(Lang::Str::ErrStreamInvalid), period.error().GetMessage())));
        }

        const AudioStreamInfo& info = m_streams[i].GetInfo();

        ComPtr<IMMDevice> device;
        const HRESULT hr = m_enumerator->GetDefaultAudioEndpoint(info.dataFlow, info.role, device.GetAddressOf());
        if (FAILED(hr)) {
            return tl::make_unexpected(WindowsError(fmt::format(
                Lang::Utf8(Lang::Str::ErrDefaultEndpointQuery), DescribeHResult(static_cast<long>(hr)))));
        }

        const std::wstring defaultDeviceId = GetDeviceId(device.Get());
        if (!SameDevice(defaultDeviceId, info.deviceId)) {
            return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrDefaultDeviceChanged)));
        }
    }

    return {};
}
