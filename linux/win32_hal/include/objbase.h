#pragma once
// linux/win32_hal/include/objbase.h - COM initialization, streams, variants, and drag-drop types.

#include <unknwn.h>

#define COINIT_APARTMENTTHREADED 0x2u
#define COINIT_MULTITHREADED     0x0u
#define COINIT_DISABLE_OLE1DDE   0x4u
#define COINIT_SPEED_OVER_MEMORY 0x8u

#define CLSCTX_INPROC_SERVER 0x1u
#define CLSCTX_INPROC_HANDLER 0x2u
#define CLSCTX_LOCAL_SERVER 0x4u
#define CLSCTX_REMOTE_SERVER 0x10u
#define CLSCTX_ALL (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)

#define COWAIT_DEFAULT 0
#define COWAIT_WAITALL 1
#define COWAIT_ALERTABLE 2
#define COWAIT_INPUTAVAILABLE 4
#define COWAIT_DISPATCH_CALLS 8
#define COWAIT_DISPATCH_WINDOW_MESSAGES 0x10

#define STATFLAG_DEFAULT 0u
#define STATFLAG_NONAME 1u

#define STREAM_SEEK_SET 0u
#define STREAM_SEEK_CUR 1u
#define STREAM_SEEK_END 2u

struct STATSTG {
    LPOLESTR pwcsName;
    DWORD type;
    ULARGE_INTEGER cbSize;
    FILETIME mtime;
    FILETIME ctime;
    FILETIME atime;
    DWORD grfMode;
    DWORD grfLocksSupported;
    CLSID clsid;
    DWORD grfStateBits;
    DWORD reserved;
};

struct ISequentialStream : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* pcbWritten) = 0;
};

struct IStream : public ISequentialStream {
    virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Revert() = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) = 0;
};
using LPSTREAM = IStream*;

E2_DECLARE_UUID(ISequentialStream, 0x0c733a30u, 0x2a1cu, 0x11ceu, 0xadu, 0xe5u, 0x00u, 0xaau, 0x00u, 0x44u, 0x77u, 0x3du)
E2_DECLARE_UUID(IStream, 0x0000000cu, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

// VARIANT & PROPVARIANT
#define VT_EMPTY 0
#define VT_NULL  1
#define VT_I2    2
#define VT_I4    3
#define VT_R4    4
#define VT_R8    5
#define VT_BSTR  8
#define VT_BOOL  11
#define VT_UI1   17
#define VT_UI2   18
#define VT_UI4   19
#define VT_I8    20
#define VT_UI8   21
#define VT_LPSTR 30
#define VT_LPWSTR 31

struct VARIANT {
    VARTYPE vt;
    WORD wReserved1;
    WORD wReserved2;
    WORD wReserved3;
    union {
        LONG lVal;
        SHORT iVal;
        FLOAT fltVal;
        double dblVal;
        BSTR bstrVal;
        LPSTR pszVal;
        LPWSTR pwszVal;
        WORD uiVal;
        ULONG ulVal;
        ULONGLONG ullVal;
    };
};

struct PROPVARIANT {
    VARTYPE vt;
    WORD wReserved1;
    WORD wReserved2;
    WORD wReserved3;
    union {
        SHORT iVal;
        USHORT uiVal;
        LONG lVal;
        ULONG ulVal;
        LARGE_INTEGER hVal;
        ULARGE_INTEGER uhVal;
        FLOAT fltVal;
        double dblVal;
        LPSTR pszVal;
        LPWSTR pwszVal;
        BSTR bstrVal;
    };
};

inline void VariantInit(VARIANT* pvarg) {
    if (pvarg) std::memset(pvarg, 0, sizeof(*pvarg));
}
inline HRESULT VariantClear(VARIANT* pvarg) {
    if (pvarg) std::memset(pvarg, 0, sizeof(*pvarg));
    return S_OK;
}
inline void PropVariantInit(PROPVARIANT* pvar) {
    if (pvar) std::memset(pvar, 0, sizeof(*pvar));
}
HRESULT PropVariantClear(PROPVARIANT* pvar);

BSTR SysAllocString(const OLECHAR* psz);
void SysFreeString(BSTR bstrString);

// Data transfer / Drag-and-Drop interfaces
#define TYMED_HGLOBAL 1u
#define TYMED_FILE 2u
#define TYMED_ISTREAM 4u
#define DVASPECT_CONTENT 1u

#define DROPEFFECT_NONE 0u
#define DROPEFFECT_COPY 1u
#define DROPEFFECT_MOVE 2u
#define DROPEFFECT_LINK 4u

struct DVTARGETDEVICE;
struct FORMATETC {
    CLIPFORMAT cfFormat;
    DVTARGETDEVICE* ptd;
    DWORD dwAspect;
    LONG lindex;
    DWORD tymed;
};

struct STGMEDIUM {
    DWORD tymed;
    union {
        HBITMAP hBitmap;
        HGLOBAL hGlobal;
        LPOLESTR lpszFileName;
        IStream* pstm;
    };
    IUnknown* pUnkForRelease;
};
using LPSTGMEDIUM = STGMEDIUM*;

struct IEnumFORMATETC;
struct IAdviseSink;

struct IDataObject : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) { (void)pformatetc; (void)pmedium; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) { (void)pformatetc; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) { (void)pformatectIn; (void)pformatetcOut; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) { (void)pformatetc; (void)pmedium; (void)fRelease; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) { (void)dwDirection; (void)ppenumFormatEtc; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) { (void)pformatetc; (void)advf; (void)pAdvSink; (void)pdwConnection; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection) { (void)dwConnection; return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE EnumDAdvise(void** ppenumAdvise) { (void)ppenumAdvise; return E_NOTIMPL; }
};

struct IDropSource : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) = 0;
    virtual HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) = 0;
};

E2_DECLARE_UUID(IDataObject, 0x0000010eu, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)
E2_DECLARE_UUID(IDropSource, 0x00000121u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

struct PROPBAG2 {
    DWORD dwType;
    VARTYPE vt;
    CLIPFORMAT cfType;
    DWORD dwHint;
    LPOLESTR pstrName;
    CLSID clsid;
};

struct IErrorLog;
struct IPropertyBag2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Read(ULONG cProperties, PROPBAG2* pPropBag, IErrorLog* pErrLog, VARIANT* pvarValue, HRESULT* phrError) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(ULONG cProperties, PROPBAG2* pPropBag, VARIANT* pvarValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE CountProperties(ULONG* pcProperties) { if (pcProperties) *pcProperties = 0; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE GetPropertyInfo(ULONG, ULONG, PROPBAG2*, ULONG*) { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE LoadObject(LPCOLESTR, DWORD, IUnknown*, IErrorLog*) { return E_NOTIMPL; }
};
E2_DECLARE_UUID(IPropertyBag2, 0x22f55882u, 0x280bu, 0x11d0u, 0xa8u, 0xa9u, 0x00u, 0xa0u, 0xc9u, 0x0cu, 0x20u, 0x04u)

// COM Runtime APIs
HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
HRESULT CoInitialize(LPVOID pvReserved);
void CoUninitialize();
HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv);
LPVOID CoTaskMemAlloc(SIZE_T cb);
void CoTaskMemFree(LPVOID pv);
HRESULT CoWaitForMultipleHandles(DWORD dwFlags, DWORD dwTimeout, ULONG cHandles, LPHANDLE pHandles, LPDWORD lpdwIndex);
void CoFreeUnusedLibrariesEx(DWORD dwUnloadDelay, DWORD dwReserved);
HRESULT CreateStreamOnHGlobal(HGLOBAL hGlobal, BOOL fDeleteOnRelease, LPSTREAM* ppstm);
HRESULT GetHGlobalFromStream(LPSTREAM pstm, HGLOBAL* phglobal);
void ReleaseStgMedium(LPSTGMEDIUM pmedium);
