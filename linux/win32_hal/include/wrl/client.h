#pragma once
// linux/win32_hal/include/wrl/client.h - Microsoft::WRL::ComPtr compatible smart pointer for Linux HAL.

#include <unknwn.h>
#include <type_traits>
#include <utility>

namespace Microsoft::WRL {

template <typename T>
class ComPtr {
public:
    using InterfaceType = T;

    constexpr ComPtr() noexcept : m_ptr(nullptr) {}
    constexpr ComPtr(std::nullptr_t) noexcept : m_ptr(nullptr) {}

    explicit ComPtr(T* p) noexcept : m_ptr(p) { InternalAddRef(); }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(U* p) noexcept : m_ptr(p) { InternalAddRef(); }

    ComPtr(const ComPtr& other) noexcept : m_ptr(other.m_ptr) { InternalAddRef(); }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(const ComPtr<U>& other) noexcept : m_ptr(other.Get()) { InternalAddRef(); }

    ComPtr(ComPtr&& other) noexcept : m_ptr(other.Detach()) {}

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(ComPtr<U>&& other) noexcept : m_ptr(other.Detach()) {}

    ~ComPtr() noexcept { InternalRelease(); }

    ComPtr& operator=(std::nullptr_t) noexcept {
        InternalRelease();
        return *this;
    }

    ComPtr& operator=(T* other) noexcept {
        if (m_ptr != other) {
            if (other) other->AddRef();
            InternalRelease();
            m_ptr = other;
        }
        return *this;
    }

    ComPtr& operator=(const ComPtr& other) noexcept {
        return (*this = other.m_ptr);
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr& operator=(const ComPtr<U>& other) noexcept {
        return (*this = static_cast<T*>(other.Get()));
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            InternalRelease();
            m_ptr = other.Detach();
        }
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr& operator=(ComPtr<U>&& other) noexcept {
        InternalRelease();
        m_ptr = other.Detach();
        return *this;
    }

    T* Get() const noexcept { return m_ptr; }
    T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }
    bool operator!() const noexcept { return m_ptr == nullptr; }

    T* const* GetAddressOf() const noexcept { return &m_ptr; }
    T** GetAddressOf() noexcept { return &m_ptr; }

    T** ReleaseAndGetAddressOf() noexcept {
        InternalRelease();
        return &m_ptr;
    }

    // Matches WRL ComPtr::operator& behavior: releases prior pointer and returns T** for out-params & IID_PPV_ARGS
    T** operator&() noexcept { return ReleaseAndGetAddressOf(); }
    T* const* operator&() const noexcept { return &m_ptr; }

    ULONG Reset() noexcept { return InternalRelease(); }

    void Attach(T* other) noexcept {
        if (m_ptr != other) {
            InternalRelease();
            m_ptr = other;
        }
    }

    T* Detach() noexcept {
        T* tmp = m_ptr;
        m_ptr = nullptr;
        return tmp;
    }

    void Swap(ComPtr& r) noexcept { std::swap(m_ptr, r.m_ptr); }

    template <typename U>
    HRESULT As(ComPtr<U>* p) const noexcept {
        if (!p) return E_POINTER;
        if (!m_ptr) { p->Reset(); return E_POINTER; }
        return m_ptr->QueryInterface(__uuidof(U), reinterpret_cast<void**>(p->ReleaseAndGetAddressOf()));
    }

    template <typename U>
    HRESULT As(U** pp) const noexcept {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        if (!m_ptr) return E_POINTER;
        return m_ptr->QueryInterface(__uuidof(U), reinterpret_cast<void**>(pp));
    }

    HRESULT CopyTo(T** ptr) const noexcept {
        if (!ptr) return E_POINTER;
        InternalAddRef();
        *ptr = m_ptr;
        return S_OK;
    }

private:
    void InternalAddRef() const noexcept {
        if (m_ptr) m_ptr->AddRef();
    }
    ULONG InternalRelease() noexcept {
        ULONG ref = 0;
        T* tmp = m_ptr;
        if (tmp) {
            m_ptr = nullptr;
            ref = tmp->Release();
        }
        return ref;
    }

    T* m_ptr;
};

template <typename T, typename U>
inline bool operator==(const ComPtr<T>& a, const ComPtr<U>& b) noexcept { return a.Get() == b.Get(); }
template <typename T>
inline bool operator==(const ComPtr<T>& a, std::nullptr_t) noexcept { return a.Get() == nullptr; }
template <typename T>
inline bool operator==(std::nullptr_t, const ComPtr<T>& a) noexcept { return a.Get() == nullptr; }
template <typename T, typename U>
inline bool operator!=(const ComPtr<T>& a, const ComPtr<U>& b) noexcept { return a.Get() != b.Get(); }
template <typename T>
inline bool operator!=(const ComPtr<T>& a, std::nullptr_t) noexcept { return a.Get() != nullptr; }
template <typename T>
inline bool operator!=(std::nullptr_t, const ComPtr<T>& a) noexcept { return a.Get() != nullptr; }

} // namespace Microsoft::WRL
