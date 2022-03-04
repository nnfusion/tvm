#define INIT
#include "direct_test_common.h"

FunctionInfo create_funcinfo(int argsnum) {
  std::string jsinfo0 = R"(
{
  "name": "CSMain",
  "arg_types": [
)";
  std::string jsinfo2 = R"(
    "handle"
  ],
  "launch_param_tags": [
    "blockIdx.x",
    "threadIdx.x"
  ]
}
)";
  std::string js = "";
  while (argsnum > 1) {
    argsnum--;
    jsinfo0 += "\n\"handle\", \n";
  }
  if (argsnum <= 1) js = jsinfo0 + jsinfo2;
  std::istringstream is(js);
  dmlc::JSONReader jsr(&is);
  FunctionInfo fi;
  fi.Load(&jsr);
  return fi;
}

TEST(DirectX, single_tensor_int_increase_kernel) {
  DirectXContext dx_ctx;
  for (uint32_t i = 0; i < dx_ctx.get_devices_count(); ++i) {
    auto dev = dx_ctx.get_device(i);
    auto kernel = dev->gen_computer_shader_from_string(hlsl_str_inc);
    kernel->set_function_info(create_funcinfo(1));
    auto host0 = dev->host_buffer(16, {kDLFloat, 32, 4});
    auto host1 =
        dev->host_buffer(16, {kDLFloat, 32, 4}, DirectHostBuffer::hostbuffer_state::readback);
    set_tensor<int>(host0, {1, 2, 3, 4});
    auto device0 = dev->device_buffer(16, {kDLFloat, 32, 4});
    dev->buffer_copy(device0, host0);
    dev->dispatch(kernel, {device0});
    dev->buffer_copy(host1, device0);
    check_tensor<int>(host1, {2, 3, 4, 5});
  }
}

TEST(DirectX, two_tensor_add_kernel) {
  DirectXContext dx_ctx;
  for (uint32_t i = 0; i < dx_ctx.get_devices_count(); ++i) {
    auto dev = dx_ctx.get_device(i);
    auto kernel = dev->gen_computer_shader_from_string(hlsl_str_add);
    kernel->set_function_info(create_funcinfo(3));
    auto host0 = dev->host_buffer(16, {kDLFloat, 32, 4});
    auto host1 = dev->host_buffer(16, {kDLFloat, 32, 4});
    auto host2 =
        dev->host_buffer(16, {kDLFloat, 32, 4}, DirectHostBuffer::hostbuffer_state::readback);
    auto device0 = dev->device_buffer(16, {kDLFloat, 32, 4});
    auto device1 = dev->device_buffer(16, {kDLFloat, 32, 4});
    auto device2 = dev->device_buffer(16, {kDLFloat, 32, 4});
    set_tensor<int>(host0, {0, 1, 2, 3});
    set_tensor<int>(host1, {10, 11, 12, 13});
    dev->buffer_copy(device0, host0);
    dev->buffer_copy(device1, host1);
    dev->dispatch(kernel, {device0, device1, device2});
    dev->buffer_copy(host2, device2);
    check_tensor<int>(host2, {10, 12, 14, 16});
  }
}

TEST(DirectX, memcpy_range_copy) {
  DirectXContext dx_ctx;
  for (uint32_t i = 0; i < dx_ctx.get_devices_count(); ++i) {
    auto dev = dx_ctx.get_device(i);
    auto host0 = dev->host_buffer(16, {kDLFloat, 32, 4});
    auto device0 = dev->device_buffer(16, {kDLFloat, 32, 4});
    auto host1 =
        dev->host_buffer(16, {kDLFloat, 32, 4}, DirectHostBuffer::hostbuffer_state::readback);
    auto host2 =
        dev->host_buffer(16, {kDLFloat, 32, 4}, DirectHostBuffer::hostbuffer_state::upload);

    set_tensor<int>(host0, {1, 2, 3, 4});
    dev->buffer_copy(device0, 4, host0, 4, 8, false);
    dev->buffer_copy(host1, 4, device0, 4, 8, false);
    check_tensor<int>(host1, {0, 2, 3, 0});
    dev->buffer_copy(host0, 0, host1, 4, 8, false);
    check_tensor<int>(host0, {2, 3, 3, 4});
    dev->buffer_copy(host2, 0, device0, 0, 16, false);
    check_tensor<int>(host2, {0, 2, 3, 0});
  }
}
