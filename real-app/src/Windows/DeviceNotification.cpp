#include "DeviceNotification.h"

#include <unknwn.h>

using namespace miniant::Windows;

DeviceNotificationClient::DeviceNotificationClient(DeviceEventHandler handler):
    m_handler(std::move(handler)) {}

DeviceNotificationClient::~DeviceNotificationClient() = default;

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::QueryInterface(REFIID riid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }

    *object = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DeviceNotificationClient::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE DeviceNotificationClient::Release() {
    return static_cast<ULONG>(::InterlockedDecrement(&m_refCount));
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) {
    if (m_handler) {
        DeviceEvent event;
        event.type = DeviceEventType::DeviceStateChanged;
        event.deviceId = deviceId == nullptr ? L"" : deviceId;
        event.state = newState;
        m_handler(event);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceAdded(LPCWSTR deviceId) {
    if (m_handler) {
        DeviceEvent event;
        event.type = DeviceEventType::DeviceAdded;
        event.deviceId = deviceId == nullptr ? L"" : deviceId;
        m_handler(event);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceRemoved(LPCWSTR deviceId) {
    if (m_handler) {
        DeviceEvent event;
        event.type = DeviceEventType::DeviceRemoved;
        event.deviceId = deviceId == nullptr ? L"" : deviceId;
        m_handler(event);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) {
    if (m_handler) {
        DeviceEvent event;
        event.type = DeviceEventType::DefaultDeviceChanged;
        event.dataFlow = flow;
        event.role = role;
        event.deviceId = defaultDeviceId == nullptr ? L"" : defaultDeviceId;
        m_handler(event);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnPropertyValueChanged(LPCWSTR /*deviceId*/, const PROPERTYKEY /*key*/) {
    // Not interesting for the latency reduction (volume, name and similar changes
    // arrive here very often), so this event is deliberately ignored.
    return S_OK;
}
