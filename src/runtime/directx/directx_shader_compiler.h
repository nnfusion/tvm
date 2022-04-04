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

struct DxilMinimalHeader {
  char four_cc[4];
  uint32_t hash_digest[4];
};

long check_dxil(void* buffer, bool must_signed = false);

void dxc_compile(const std::string& src, std::string entry_point, std::string profile, void** pshader, void** preflection);

}  // namespace dxc
}  // namespace dx
}  // namespace runtime
}  // namespace tvm