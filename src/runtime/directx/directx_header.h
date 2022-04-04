#pragma once

#include <exception>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <regex>
#include <type_traits>

// Thirdparty
#include "dlpack/dlpack.h"
#include "dmlc/json.h"
// Adapter for non-windows
#ifndef _WIN32
#include "wsl/winadapter.h"
#endif

#include "directx/d3d12.h"
#include "directx/d3d12sdklayers.h"
#include "directx/d3dx12.h"
#include "directx/dxcore.h"
#include "directx/dxcore_interface.h"
#include "dxguids/dxguids.h"
#include "directx_d3d12shader.h"

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof(*(a)))
#endif

#ifndef ZeroMemory
#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

#ifdef _WIN32
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcore.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#endif  // !DISALLOW_COPY_AND_ASSIGN

#ifndef _msg_
#define _msg_(arg)                                                       \
  (std::string(__FILE__) + std::string(":") + std::to_string(__LINE__) + \
   std::string(" throw message - ") + std::string(arg))
#endif

// Coding style here is mixed of windows, std c++ and linux
// Becasue some interface of DirectX need windows api/DS,
// and for cross-platform compatiblity we need stl and so on..

#ifndef DXGI_ERROR_NOT_FOUND
#define DXGI_ERROR_NOT_FOUND 0x887A0002
#endif

#ifndef DXGI_ERROR_MORE_DATA
#define DXGI_ERROR_MORE_DATA 0x887A0003
#endif

#ifndef DXGI_ERROR_UNSUPPORTED
#define DXGI_ERROR_UNSUPPORTED 0x887A0004
#endif

namespace DirectX {
inline const char* GetErrorString(HRESULT error) {
  switch (error) {
    case S_OK:
      return "S_OK";
    case S_FALSE:
      return "S_FALSE";
    case E_NOTIMPL:
      return "E_NOTIMPL";
    case E_OUTOFMEMORY:
      return "E_OUTOFMEMORY";
    case E_INVALIDARG:
      return "E_INVALIDARG";
    case E_NOINTERFACE:
      return "E_NOINTERFACE";
    case E_POINTER:
      return "E_POINTER";
    case E_HANDLE:
      return "E_HANDLE";
    case E_ABORT:
      return "E_ABORT";
    case E_FAIL:
      return "E_FAIL";
    case E_ACCESSDENIED:
      return "E_ACCESSDENIED";
    case E_UNEXPECTED:
      return "E_UNEXPECTED";
    case DXGI_ERROR_INVALID_CALL:
      return "DXGI_ERROR_INVALID_CALL";
    case DXGI_ERROR_NOT_FOUND:
      return "DXGI_ERROR_NOT_FOUND";
    case DXGI_ERROR_MORE_DATA:
      return "DXGI_ERROR_MORE_DATA";
    case DXGI_ERROR_UNSUPPORTED:
      return "DXGI_ERROR_UNSUPPORTED";
    case DXGI_ERROR_DEVICE_REMOVED:
      return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_HUNG:
      return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_RESET:
      return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    default:
      return "UNKNOWN";
  }
}

// Helper class for COM exceptions
class com_exception : public std::exception {
 public:
  com_exception(HRESULT hr, std::string file, std::string line) noexcept
      : result(hr), file(file), line(line) {}

  const char* what() const noexcept override {
    static std::string msg;
    std::ostringstream oss;
    oss.clear();
    oss << file << ":" << line << " throw message - Failure with HRESULT of " << std::hex
        << static_cast<unsigned int>(result) << ", " << GetErrorString(result);
    msg = oss.str();
    return msg.c_str();
  }

  HRESULT get_result() const noexcept { return result; }

 private:
  HRESULT result;
  std::string file, line;
};

#define ThrowIfFailed(expr)                                                   \
{                                                                             \
  HRESULT hr = expr;                                                          \
  if (FAILED(hr)) {                                                           \
    throw com_exception(hr, std::string(__FILE__), std::to_string(__LINE__)); \
  }                                                                           \
}
}  // namespace DirectX

using namespace Microsoft::WRL;
using namespace DirectX;

namespace tvm {
namespace runtime {
namespace dx {

using ID3DComputerShader = ID3DBlob;
class DirectComputeKernel;
class DirectXDevice;
class DirectXContext;

namespace dxc {
struct IUnknown {
  IUnknown() : m_count(0){};
  virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG AddRef();
  virtual ULONG Release();
// todo(wenxh): Hacked code for alignment of virtual functions
#ifndef _WIN32
  virtual ~IUnknown();
#endif

 private:
  std::atomic<unsigned long> m_count;
};

// IDxcBlob is an alias of ID3D10Blob and ID3DBlob
struct IDxcBlob : public IUnknown {
 public:
  virtual LPVOID STDMETHODCALLTYPE GetBufferPointer(void) = 0;
  virtual SIZE_T STDMETHODCALLTYPE GetBufferSize(void) = 0;
};

DECLARE_INTERFACE_(ID3D12ShaderReflection, IUnknown)
{
  STDMETHOD(QueryInterface)(THIS_ _In_ REFIID iid,
                           _Out_ LPVOID *ppv) PURE;
  STDMETHOD_(ULONG, AddRef)(THIS) PURE;
  STDMETHOD_(ULONG, Release)(THIS) PURE;

  STDMETHOD(GetDesc)(THIS_ _Out_ D3D12_SHADER_DESC *pDesc) PURE;

  STDMETHOD_(ID3D12ShaderReflectionConstantBuffer*, GetConstantBufferByIndex)(THIS_ _In_ UINT Index) PURE;
  STDMETHOD_(ID3D12ShaderReflectionConstantBuffer*, GetConstantBufferByName)(THIS_ _In_ LPCSTR Name) PURE;

  STDMETHOD(GetResourceBindingDesc)(THIS_ _In_ UINT ResourceIndex,
                                    _Out_ D3D12_SHADER_INPUT_BIND_DESC *pDesc) PURE;

  STDMETHOD(GetInputParameterDesc)(THIS_ _In_ UINT ParameterIndex,
                                   _Out_ D3D12_SIGNATURE_PARAMETER_DESC *pDesc) PURE;
  STDMETHOD(GetOutputParameterDesc)(THIS_ _In_ UINT ParameterIndex,
                                    _Out_ D3D12_SIGNATURE_PARAMETER_DESC *pDesc) PURE;
  STDMETHOD(GetPatchConstantParameterDesc)(THIS_ _In_ UINT ParameterIndex,
                                          _Out_ D3D12_SIGNATURE_PARAMETER_DESC *pDesc) PURE;

  STDMETHOD_(ID3D12ShaderReflectionVariable*, GetVariableByName)(THIS_ _In_ LPCSTR Name) PURE;

  STDMETHOD(GetResourceBindingDescByName)(THIS_ _In_ LPCSTR Name,
                                         _Out_ D3D12_SHADER_INPUT_BIND_DESC *pDesc) PURE;

  STDMETHOD_(UINT, GetMovInstructionCount)(THIS) PURE;
  STDMETHOD_(UINT, GetMovcInstructionCount)(THIS) PURE;
  STDMETHOD_(UINT, GetConversionInstructionCount)(THIS) PURE;
  STDMETHOD_(UINT, GetBitwiseInstructionCount)(THIS) PURE;

  STDMETHOD_(D3D_PRIMITIVE, GetGSInputPrimitive)(THIS) PURE;
  STDMETHOD_(BOOL, IsSampleFrequencyShader)(THIS) PURE;

  STDMETHOD_(UINT, GetNumInterfaceSlots)(THIS) PURE;
  STDMETHOD(GetMinFeatureLevel)(THIS_ _Out_ enum D3D_FEATURE_LEVEL* pLevel) PURE;

  STDMETHOD_(UINT, GetThreadGroupSize)(THIS_
                                      _Out_opt_ UINT* pSizeX,
                                      _Out_opt_ UINT* pSizeY,
                                      _Out_opt_ UINT* pSizeZ) PURE;

  STDMETHOD_(UINT64, GetRequiresFlags)(THIS) PURE;
};
}  // namespace dxc

}  // namespace dx
}  // namespace runtime
}  // namespace tvm

#include "directx_buffer.h"
#include "directx_context.h"
#include "directx_device.h"
#include "directx_kernel.h"
