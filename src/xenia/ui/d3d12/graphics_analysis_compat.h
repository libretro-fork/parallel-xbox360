/* Minimal graphics-capture COM ABI for SDKs missing DXProgrammableCapture.h.
 * Core-owned declarations, usable from MSVC C89 and C++. BSD, see LICENSE. */
#ifndef XE_GRAPHICS_ANALYSIS_COMPAT_H
#define XE_GRAPHICS_ANALYSIS_COMPAT_H
#include <unknwn.h>

#undef INTERFACE
#define INTERFACE IDXGraphicsAnalysis
DECLARE_INTERFACE_(IDXGraphicsAnalysis, IUnknown) {
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **object) PURE;
  STDMETHOD_(ULONG, AddRef)(THIS) PURE;
  STDMETHOD_(ULONG, Release)(THIS) PURE;
  STDMETHOD_(void, BeginCapture)(THIS) PURE;
  STDMETHOD_(void, EndCapture)(THIS) PURE;
};
#undef INTERFACE

#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IDXGraphicsAnalysis, 0x9f251514, 0x9d4d, 0x4902,
                0x9d, 0x60, 0x18, 0x98, 0x8a, 0xb7, 0xd4, 0xb5)
#endif
#endif
