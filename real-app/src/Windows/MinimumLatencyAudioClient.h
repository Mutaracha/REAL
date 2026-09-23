#pragma once

#include "WindowsError.h"

#include <tl/expected.hpp>

#include <Windows.h>

#include <Audioclient.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <string>

namespace miniant::Windows::WasapiLatency {

enum class PeriodSelection {
    Minimum,
    Fundamental,
    Fixed,
};

struct AudioStreamInfo {
    std::wstring deviceName;
    std::wstring deviceId;
    EDataFlow dataFlow = eRender;
    ERole role = eConsole;

    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;

    uint32_t defaultPeriod = 0;
    uint32_t fundamentalPeriod = 0;
    uint32_t minPeriod = 0;
    uint32_t maxPeriod = 0;
    uint32_t requestedPeriod = 0;
    uint32_t currentPeriod = 0;

    // True when the driver does not offer anything smaller than its default period.
    bool lowLatencyNotAvailable = false;
    // True when the engine period was already locked by another application.
    bool acceptedLockedPeriod = false;

    double PeriodMilliseconds(uint32_t frames) const;
};

// Holds a running shared-mode WASAPI stream that requests the smallest period
// supported by the endpoint driver. While such a stream exists, the Windows
// audio engine renders every client of that endpoint with the small buffer.
class MinimumLatencyAudioClient {
public:
    MinimumLatencyAudioClient() = default;
    MinimumLatencyAudioClient(MinimumLatencyAudioClient&& other) noexcept;
    MinimumLatencyAudioClient& operator=(MinimumLatencyAudioClient&& rhs) noexcept;
    ~MinimumLatencyAudioClient();

    MinimumLatencyAudioClient(const MinimumLatencyAudioClient&) = delete;
    MinimumLatencyAudioClient& operator=(const MinimumLatencyAudioClient&) = delete;

    void Stop();
    bool IsActive() const;

    const AudioStreamInfo& GetInfo() const;

    // Returns the engine period currently used for the endpoint. Fails when the
    // stream has been invalidated (device removed, audio service restarted).
    tl::expected<uint32_t, WindowsError> GetCurrentPeriod();

    static tl::expected<MinimumLatencyAudioClient, WindowsError> Start(
        IMMDeviceEnumerator& enumerator,
        EDataFlow dataFlow,
        ERole role,
        PeriodSelection selection,
        uint32_t requestedPeriodFrames,
        bool allowPeriodSnap);

private:
    IAudioClient3* m_audioClient = nullptr;
    WAVEFORMATEX* m_format = nullptr;
    AudioStreamInfo m_info;
};

std::string DescribeStreamWin32(const AudioStreamInfo& info);

}
