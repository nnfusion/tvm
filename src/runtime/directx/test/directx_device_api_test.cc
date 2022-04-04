#include "direct_test_common.h"
#include <tvm/runtime/ndarray.h>

TEST(DirectX, NDArray) {
  auto arr = NDArray::Empty({1}, {kDLFloat, 32, 1}, create_device(kDLDirectX, 0));
}