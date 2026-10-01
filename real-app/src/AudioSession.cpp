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

// Windows keeps a separate default device for every role: the usual "default
// device" that the Sound settings show is the console (and multimedia) role,
// and next to it there is the "default communication device". The latency
// reduction has to hold a stream on each of them, otherwise a program that
// plays through the communication device would keep the large buffer.
std::vector<ERole> AllRoles() {
    return { eConsole, eMultimedia, eCommunications };
}

// An endpoint of the default devices: the device itself, its flow and the role
// of the default device it was found by.
struct DefaultEndpoint {
    ComPtr<IMMDevice> device;
    EDataFlow dataFlow = eRender;
    ERole role = eConsole;
    std::wstring deviceId;
};

// Adds the endpoint to the list unless the same device is already there: the
// console and the multimedia roles almost always point at one device, and the
// communication device may be the same one as well.
void AddEndpoint(std::vector<DefaultEndpoint>& endpoints, IMMDeviceEnumerator& enumerator, EDataFlow flow, ERole role) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator.GetDefaultAudioEndpoint(flow, role, device.GetAddressOf())) || !device) {
        return;
    }

    const std::wstring id = GetDeviceId(device.Get());

    for (const DefaultEndpoint& existing : endpoints) {
        if (!id.empty() && existing.deviceId == id) {
            return;
        }
    }

    DefaultEndpoint endpoint;
    endpoint.device = std::move(device);
    endpoint.dataFlow = flow;
    endpoint.role = role;
    endpoint.deviceId = id;
    endpoints.push_back(std::move(endpoint));
}

std::vector<DefaultEndpoint> GetDefaultEndpoints(IMMDeviceEnumerator& enumerator, const std::vector<EDataFlow>& flows) {
    std::vector<DefaultEndpoint> endpoints;

    for (EDataFlow flow : flows) {
        for (ERole role : AllRoles()) {
            AddEndpoint(endpoints, enumerator, flow, role);
        }
    }

    return endpoints;
}

// The identifiers of the devices that are the default ones for the flow right
// now, whatever role they are default for.
std::vector<std::wstring> GetDefaultDeviceIds(IMMDeviceEnumerator& enumerator, EDataFlow flow) {
    std::vector<std::wstring> ids;

    for (ERole role : AllRoles()) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(enumerator.GetDefaultAudioEndpoint(flow, role, device.GetAddressOf())) && device) {
            ids.push_back(GetDeviceId(device.Get()));
        }
    }

    return ids;
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

    // Every default device of the chosen flows: a program that plays through
    // the default communication device gets the small buffer just like one that
    // plays through the usual default device.
    const std::vector<DefaultEndpoint> endpoints = GetDefaultEndpoints(*m_enumerator.Get(), flows);

    for (const DefaultEndpoint& endpoint : endpoints) {
        auto stream = MinimumLatencyAudioClient::Start(
            *endpoint.device.Get(),
            endpoint.dataFlow,
            endpoint.role,
            ToPeriodSelection(settings.audio.periodSelection),
            settings.audio.requestedPeriodFrames,
            settings.audio.allowPeriodSnap);

        if (!stream) {
            const std::string message =
                std::string(Lang::Utf8(endpoint.dataFlow == EDataFlow::eRender ? Lang::Str::FlowRender : Lang::Str::FlowCapture))
                + ": " + stream.error().GetMessage();
            // The message is handed to the caller: the failure is reported
            // once, when the whole apply is over (see App::ApplyAudio).
            errors.push_back(message);
            continue;
        }

        const AudioStreamInfo& info = stream->GetInfo();

        if (!stream->IsActive()) {
            // The driver has nothing smaller than the default buffer, so no
            // stream is held (see MinimumLatencyAudioClient::Start); the result
            // line of the log reports it.
            m_streamsInfo.push_back(info);
            continue;
        }

        // Detail level: the result line of the log already reports the device,
        // the format and the buffer size; the flow is only needed when looking
        // into a specific problem.
        Log::Debug(Lang::Utf8(Lang::Str::LogLowLatencyStarted), DescribeStreamWin32(info));

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

std::string AudioSession::GetDetailsText() const {
    if (m_streamsInfo.empty()) {
        return Lang::Utf8(Lang::Str::StatusNotActive);
    }

    return Windows::WasapiLatency::DescribeStreamForStatus(m_streamsInfo.front());
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

        // The stream is valid while its device is one of the default devices of
        // its flow: a change of any role (the usual default device or the
        // communication one) means the stream has to be built again.
        const std::vector<std::wstring> defaultIds = GetDefaultDeviceIds(*m_enumerator.Get(), info.dataFlow);

        if (defaultIds.empty()) {
            return tl::make_unexpected(WindowsError(fmt::format(
                Lang::Utf8(Lang::Str::ErrDefaultEndpointQuery), DescribeHResult(static_cast<long>(E_POINTER)))));
        }

        bool isDefault = info.deviceId.empty();

        for (const std::wstring& id : defaultIds) {
            if (SameDevice(id, info.deviceId)) {
                isDefault = true;
                break;
            }
        }

        if (!isDefault) {
            return tl::make_unexpected(WindowsError(Lang::Utf8(Lang::Str::ErrDefaultDeviceChanged)));
        }
    }

    return {};
}
