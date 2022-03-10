#pragma once
// dxcapi.h has conflicts with DirectX headers if including them at sametime
#include <string>

#ifdef _WIN32
#pragma comment(lib, "dxcompiler.lib")
#endif

namespace tvm {
namespace runtime {
namespace dx {

// Return value is a pointer to ID3DBlob
void* dxcompile(const std::string& src, std::string entry_point, std::string profile);

}  // namespace dx
}  // namespace runtime
}  // namespace tvm