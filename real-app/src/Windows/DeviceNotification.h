#pragma once

#include <Windows.h>

#include <mmdeviceapi.h>

#include <functional>
#include <string>

namespace miniant::Windows {

// A change of a default device: its flow, its role and the endpoint that is the
// default one now (empty when there is none left).
struct DeviceEvent {
    EDataFlow dataFlow = eRender;
    ERole role = eConsole;
    std::wstring deviceId;
};

using DeviceEventHandler = std::function<void(const DeviceEvent&)>;

// Receives endpoint notifications from the audio service (IMMNotificationClient)
// and hands the changes of the default devices to the handler. The program
// holds its streams on the default devices only, and a default device that goes
// away or comes back is reported as a change of the default device as well:
// the other notifications concern any device (a microphone, the sound of a
// monitor that falls asleep) and are not passed on.
//
// The callbacks are invoked on a thread owned by the audio system, so the
// handler must not call into the audio client: it should only post a message to
// the main window. The audio objects stay owned by the main thread.
// "final": the object is owned and deleted through its own type (unique_ptr in
// AudioSession), never through the COM interface, which has no virtual
// destructor.
class DeviceNotificationClient final: public IMMNotificationClient {
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
