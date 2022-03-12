#include "directx_shader_compiler.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d12.h>
#include <atlbase.h>
#include "dxc\Support\d3dx12.h"
using namespace ATL;
#endif

#include <stdexcept>
#include "dxc/dxcapi.h"

namespace tvm {
namespace runtime {
namespace dx {
namespace dxc {

long check_dxil(void* buffer, bool must_signed) {
  DxilMinimalHeader* header = reinterpret_cast<DxilMinimalHeader*>(buffer);
  try {
    bool has_header = true;
    has_header = has_header && (header->four_cc[0] == 'D');
    has_header = has_header && (header->four_cc[1] == 'X');
    has_header = has_header && (header->four_cc[2] == 'B');
    has_header = has_header && (header->four_cc[3] == 'C');
    bool has_digest = false;
    has_digest |= header->hash_digest[0] != 0x0;
    has_digest |= header->hash_digest[1] != 0x0;
    has_digest |= header->hash_digest[2] != 0x0;
    has_digest |= header->hash_digest[3] != 0x0;
    if (has_header && (has_digest || !must_signed)) return 0;
    return E_INVALIDARG;
  } catch(std::exception e) {
    return E_ACCESSDENIED;
  }
}

// Based on https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll
void dxc_compile(const std::string& src, std::string entry_point, std::string profile, void** pshader) {
  std::wstring w_entry_point(entry_point.begin(), entry_point.end());
  std::wstring w_profile(profile.begin(), profile.end());
  std::wstring w_file_name = w_entry_point + L"_kernel.hlsl";

  CComPtr<IDxcUtils> pUtils;
  CComPtr<IDxcCompiler3> pCompiler;
  DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
  DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));
  CComPtr<IDxcIncludeHandler> pIncludeHandler;
  pUtils->CreateDefaultIncludeHandler(&pIncludeHandler);

  LPCWSTR pszArgs[] = {
      w_file_name.c_str(), L"-E", w_entry_point.c_str(), L"-T", w_profile.c_str(),
  };

  // Load source code
  CComPtr<IDxcBlobEncoding> pSource = nullptr;
  pUtils->CreateBlobFromPinned(src.c_str(), src.length(), CP_ACP, &pSource);
  DxcBuffer Source;
  Source.Ptr = pSource->GetBufferPointer();
  Source.Size = pSource->GetBufferSize();
  Source.Encoding = CP_ACP;

  // Compile
  CComPtr<IDxcResult> pResults;
  pCompiler->Compile(&Source, pszArgs, _countof(pszArgs), pIncludeHandler, IID_PPV_ARGS(&pResults));

  // Check output
  CComPtr<IDxcBlobUtf8> pErrors = nullptr;
  pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
  // This will have all message
  if (pErrors != nullptr && pErrors->GetStringLength() != 0)
    wprintf(L"Warnings and Errors:\n%S\n", pErrors->GetStringPointer());

  // Check if failed
  HRESULT hrStatus;
  pResults->GetStatus(&hrStatus);
  if (FAILED(hrStatus)) throw std::runtime_error("Compilation Failed\n");

  // Retrieve shader
  pResults->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob), pshader, nullptr);
}
}  // namespace dxc
}  // namespace dx
}  // namespace runtime
}  // namespace tvm