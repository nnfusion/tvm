#pragma once
// dxcapi.h has conflicts with DirectX headers if including them at sametime
#include <string>

#ifdef _WIN32
#pragma comment(lib, "dxcompiler.lib")
#endif

namespace tvm {
namespace runtime {
namespace dx {
namespace dxc {

void dxc_compile(const std::string& src, std::string entry_point, std::string profile, void** pshader);

}  // namespace dxc
}  // namespace dx
}  // namespace runtime
}  // namespace tvm