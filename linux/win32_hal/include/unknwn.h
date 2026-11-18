#pragma once
// linux/win32_hal/include/unknwn.h - Minimal COM IUnknown & UUID trait system for Linux HAL.

#include <windows.h>
#include <type_traits>

#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define IFACEMETHODIMP HRESULT STDMETHODCALLTYPE
#define IFACEMETHODIMP_(type) type STDMETHODCALLTYPE

struct IUnknown {
    virtual ~IUnknown() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
using LPUNKNOWN = IUnknown*;

namespace Explorer2::HalCom {

template <typename T>
struct UuidTrait {
    static constexpr GUID value = { 0, 0, 0, { 0,0,0,0,0,0,0,0 } };
};

template <typename T>
inline constexpr const GUID& UuidOfType() noexcept {
    return UuidTrait<std::remove_cv_t<std::remove_reference_t<T>>>::value;
}

template <typename T> struct PtrPtrTarget;
template <typename T> struct PtrPtrTarget<T**> { using type = std::remove_cv_t<T>; };
template <typename T> struct PtrPtrTarget<T* const*> { using type = std::remove_cv_t<T>; };

template <typename T>
inline void** IidPpvArgsHelper(T** pp) noexcept {
    static_assert(std::is_base_of_v<IUnknown, T>, "IID_PPV_ARGS requires an IUnknown-derived type");
    return reinterpret_cast<void**>(pp);
}

// Helper CRTP / base class for Linux HAL COM objects
template <typename... Interfaces>
class ComObjectBase : public Interfaces... {
public:
    virtual ~ComObjectBase() = default;
};

} // namespace Explorer2::HalCom

#define E2_DECLARE_UUID(Type, d1, d2, d3, b0, b1, b2, b3, b4, b5, b6, b7) \
    namespace Explorer2::HalCom { \
    template <> struct UuidTrait<Type> { \
        static constexpr GUID value = { d1, d2, d3, { b0, b1, b2, b3, b4, b5, b6, b7 } }; \
    }; \
    }

E2_DECLARE_UUID(IUnknown, 0x00000000u, 0x0000u, 0x0000u, 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)
inline constexpr IID IID_IUnknown = Explorer2::HalCom::UuidTrait<IUnknown>::value;

#ifndef __uuidof
#define __uuidof(Type) (::Explorer2::HalCom::UuidOfType<Type>())
#endif

#ifndef IID_PPV_ARGS
#define IID_PPV_ARGS(ppType) \
    (::Explorer2::HalCom::UuidOfType<typename ::Explorer2::HalCom::PtrPtrTarget<std::remove_reference_t<decltype(ppType)>>::type>()), \
    (::Explorer2::HalCom::IidPpvArgsHelper(ppType))
#endif
