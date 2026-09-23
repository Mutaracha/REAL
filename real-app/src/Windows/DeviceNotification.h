#pragma once

#include <Windows.h>

#include <mmdeviceapi.h>

#include <functional>
#include <string>

namespace miniant::Windows {

enum class DeviceEventType {
    DefaultDeviceChanged,
    DeviceStateChanged,
    DeviceAdded,
    DeviceRemoved,
};

struct DeviceEvent {
    DeviceEventType type = DeviceEventType::DefaultDeviceChanged;
    EDataFlow dataFlow = eRender;
    ERole role = eConsole;
    std::wstring deviceId;
    DWORD state = 0;
};

using DeviceEventHandler = std::function<void(const DeviceEvent&)>;

// Receives endpoint notifications from the audio service (IMMNotificationClient).
//
// The callbacks are invoked on a thread owned by the audio system, so the
// handler must not call into the audio client: it should only post a message to
// the main window. The audio objects stay owned by the main thread.
class DeviceNotificationClient: public IMMNotificationClient {
public:
    explicit DeviceNotificationClient(DeviceEventHandler handler);
    ~DeviceNotificationClient();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override;

private:
    LONG m_refCount = 1;
    DeviceEventHandler m_handler;
};

}
