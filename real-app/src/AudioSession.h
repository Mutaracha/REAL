#pragma once

#include "Settings.h"
#include "Windows/ComPtr.h"
#include "Windows/DeviceNotification.h"
#include "Windows/MinimumLatencyAudioClient.h"
#include "Windows/WindowsError.h"

#include <tl/expected.hpp>

#include <memory>
#include <string>
#include <vector>

namespace miniant::Audio {

// Owns the audio engine objects of the application.
//
// All methods must be called from the thread that called Initialize() (the main
// thread, which owns the COM apartment and the message loop).
class AudioSession {
public:
    AudioSession();
    ~AudioSession();

    AudioSession(const AudioSession&) = delete;
    AudioSession& operator=(const AudioSession&) = delete;

    // Registers for endpoint notifications. The handler is invoked on a system
    // thread and must only post a message to the main window.
    tl::expected<void, Windows::WindowsError> Initialize(Windows::DeviceEventHandler handler);
    void Shutdown();

    // Starts (or restarts) the low latency streams for the configured endpoints.
    tl::expected<void, Windows::WindowsError> Apply(const Config::Settings& settings);

    void Stop();

    bool IsActive() const;

    const std::vector<Windows::WasapiLatency::AudioStreamInfo>& GetStreams() const;

    // Short status line, e.g. "2.67 ms - Speakers (Realtek Audio)".
    std::wstring GetStatusText() const;

    // Detects a stream that has been invalidated (device removed, audio service
    // restarted) or a default device that is not the one we are using anymore.
    tl::expected<void, Windows::WindowsError> Validate();

    ::IMMDeviceEnumerator* GetEnumerator() const;

private:
    Windows::ComPtr<::IMMDeviceEnumerator> m_enumerator;
    std::unique_ptr<Windows::DeviceNotificationClient> m_notification;
    std::vector<Windows::WasapiLatency::MinimumLatencyAudioClient> m_streams;
    std::vector<Windows::WasapiLatency::AudioStreamInfo> m_streamsInfo;
    bool m_notificationsRegistered = false;
};

}
