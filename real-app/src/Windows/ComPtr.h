#pragma once

namespace miniant::Windows {

// Minimal intrusive pointer for COM interfaces (avoids a dependency on WRL/ATL).
template <typename T>
class ComPtr {
public:
    ComPtr() = default;

    ~ComPtr() {
        Reset();
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** GetAddressOf() {
        Reset();
        return &m_pointer;
    }

    T* Get() const {
        return m_pointer;
    }

    T* operator->() const {
        return m_pointer;
    }

    explicit operator bool() const {
        return m_pointer != nullptr;
    }

    T* Detach() {
        T* pointer = m_pointer;
        m_pointer = nullptr;
        return pointer;
    }

    void Reset() {
        if (m_pointer != nullptr) {
            m_pointer->Release();
            m_pointer = nullptr;
        }
    }

private:
    T* m_pointer = nullptr;
};

}
