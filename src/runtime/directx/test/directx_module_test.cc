#include "direct_test_common.h"

TEST(DirectX, create_directx_hlsl_module) {
  DirectXModuleNode mod("", "hlsl", {{"CSMain", create_funcinfo(3)}}, hlsl_str_add);
  ASSERT_EQ(strcmp(mod.type_key(), "directx:hlsl"), 0);
  auto src = mod.GetSource("hlsl");
  mod.SaveToFile("test_create_directx_hlsl_module.hlsl", "hlsl");
  auto mod1 = DirectXModuleLoadFile("test_create_directx_hlsl_module.hlsl", "hlsl");
  auto src1 = mod1->GetSource("hlsl");
  ASSERT_EQ(src, src1);
}

TEST(DirectX, launch_kernel_from_hlsl_module) {
  auto wks = DirectXWorkspace::Global();
  tvm::Device cpu_dev = create_device(kDLDirectXHost, 0);
  for (size_t i = 0;; i++) {
    // Check if the device existed
    tvm::Device dev = create_device(kDLDirectX, i);
    bool existed = false;
    TVMRetValue ret;
    wks->GetAttr(dev, DeviceAttrKind::kExist, &ret);
    existed = std::move(ret);
    if (!existed) break;

    // Create module from hlsl string
    wks->SetDevice(dev);
    auto mod = DirectXModuleCreate("", "hlsl", {{"CSMain", create_funcinfo(1)}}, hlsl_str_inc);
    size_t nbytes = 4 * 4;
    void* pdev = wks->AllocDataSpace(dev, nbytes, 0, {kDLFloat, 32, 4});
    void* phost_w = wks->AllocDataSpace(cpu_dev, nbytes, 0, {kDLFloat, 32, 4});
    void* phost_r = wks->AllocDataSpace(cpu_dev, nbytes, 0, {kDLFloat, 32, 4});
    set_tensor<int>((DirectHostBuffer*)phost_w, {1, 2, 3, 4});
    wks->CopyDataFromTo(phost_w, 0, pdev, 0, nbytes, cpu_dev, dev);
    auto func_inc = mod->GetFunction("CSMain");
    func_inc(pdev, 1, 1);
    wks->CopyDataFromTo(pdev, 0, phost_r, 0, nbytes, dev, cpu_dev);
    check_tensor<int>((DirectHostBuffer*)phost_r, {2, 3, 4, 5});
    wks->FreeDataSpace(cpu_dev, phost_r);
    wks->FreeDataSpace(cpu_dev, phost_w);
    wks->FreeDataSpace(dev, pdev);
  }
}

TEST(DirectX, data_exchange) {
  auto wks = DirectXWorkspace::Global();
  tvm::Device cpu = create_device(kDLCPU, 0);
  tvm::Device cpu_host = create_device(kDLDirectXHost, 0);
  for (size_t i = 0;; i++) {
    // Check if the device existed
    tvm::Device dev = create_device(kDLDirectX, i);
    bool existed = false;
    TVMRetValue ret;
    wks->GetAttr(dev, DeviceAttrKind::kExist, &ret);
    existed = std::move(ret);
    if (!existed) break;
    wks->SetDevice(dev);
    // memcpy
    size_t nbytes = 4 * 4;
    auto cpu_ptr = wks->AllocDataSpace(cpu, nbytes, 0, {kDLFloat, 32, 4});
    auto cpu_ptr_1 = wks->AllocDataSpace(cpu, nbytes, 0, {kDLFloat, 32, 4});
    ((float*)cpu_ptr)[0] = 1.1;
    ((float*)cpu_ptr)[1] = 1.2;
    ((float*)cpu_ptr)[2] = 2.2;
    ((float*)cpu_ptr)[3] = 34.3;
    void* dev_ptr = wks->AllocDataSpace(dev, nbytes, 0, {kDLFloat, 32, 4});
    wks->CopyDataFromTo(cpu_ptr, 0, dev_ptr, 0, nbytes, cpu, dev);
    wks->CopyDataFromTo(dev_ptr, 0, cpu_ptr_1, 0, nbytes, dev, cpu);
    ASSERT_EQ(((float*)cpu_ptr)[0], ((float*)cpu_ptr_1)[0]);
    ASSERT_EQ(((float*)cpu_ptr)[1], ((float*)cpu_ptr_1)[1]);
    ASSERT_EQ(((float*)cpu_ptr)[2], ((float*)cpu_ptr_1)[2]);
    ASSERT_EQ(((float*)cpu_ptr)[3], ((float*)cpu_ptr_1)[3]);
    wks->FreeDataSpace(cpu, cpu_ptr);
    wks->FreeDataSpace(cpu, cpu_ptr_1);
    wks->FreeDataSpace(dev, dev_ptr);
  }
}

TEST(DirectX, launch_vector_add_kernel_from_hlsl_module) {
  auto wks = DirectXWorkspace::Global();
  tvm::Device cpu = create_device(kDLCPU, 0);
  tvm::Device cpu_host = create_device(kDLDirectXHost, 0);
  for (size_t i = 0;; i++) {
    // Check if the device existed
    tvm::Device dev = create_device(kDLDirectX, i);
    bool existed = false;
    TVMRetValue ret;
    wks->GetAttr(dev, DeviceAttrKind::kExist, &ret);
    existed = std::move(ret);
    if (!existed) break;
    wks->SetDevice(dev);
    // memcpy
    size_t nbytes = 4 * 4;
    auto cpu_ptr = wks->AllocDataSpace(cpu, nbytes, 0, {kDLFloat, 32, 4});
    auto cpu_ptr_0 = wks->AllocDataSpace(cpu, nbytes, 0, {kDLFloat, 32, 4});
    auto cpu_ptr_1 = wks->AllocDataSpace(cpu, nbytes, 0, {kDLFloat, 32, 4});
    ((float*)cpu_ptr)[0] = 1.1;
    ((float*)cpu_ptr)[1] = 1.2;
    ((float*)cpu_ptr)[2] = 2.2;
    ((float*)cpu_ptr)[3] = 34.3;
    ((float*)cpu_ptr_0)[0] = 11.1;
    ((float*)cpu_ptr_0)[1] = 11.2;
    ((float*)cpu_ptr_0)[2] = 12.2;
    ((float*)cpu_ptr_0)[3] = 134.3;
    void* dev_ptr = wks->AllocDataSpace(dev, nbytes, 0, {kDLFloat, 32, 4});
    void* dev_ptr_0 = wks->AllocDataSpace(dev, nbytes, 0, {kDLFloat, 32, 4});
    void* dev_ptr_1 = wks->AllocDataSpace(dev, nbytes, 0, {kDLFloat, 32, 4});
    wks->CopyDataFromTo(cpu_ptr, 0, dev_ptr, 0, nbytes, cpu, dev);
    wks->CopyDataFromTo(cpu_ptr_0, 0, dev_ptr_0, 0, nbytes, cpu, dev);
    auto mod =
        DirectXModuleCreate("", "hlsl", {{"CSMain", create_funcinfo(3)}}, hlsl_str_vector_add);
    auto vector_add = mod.GetFunction("CSMain");
    vector_add(dev_ptr_1, dev_ptr, dev_ptr_0, 1, 4);
    wks->CopyDataFromTo(dev_ptr_1, 0, cpu_ptr_1, 0, nbytes, dev, cpu);
    ASSERT_FLOAT_EQ(((float*)cpu_ptr_1)[0], 12.2);
    ASSERT_FLOAT_EQ(((float*)cpu_ptr_1)[1], 12.4);
    ASSERT_FLOAT_EQ(((float*)cpu_ptr_1)[2], 14.4);
    ASSERT_FLOAT_EQ(((float*)cpu_ptr_1)[3], 168.6);
    wks->FreeDataSpace(cpu, cpu_ptr);
    wks->FreeDataSpace(cpu, cpu_ptr_0);
    wks->FreeDataSpace(cpu, cpu_ptr_1);
    wks->FreeDataSpace(dev, dev_ptr);
    wks->FreeDataSpace(dev, dev_ptr_0);
    wks->FreeDataSpace(dev, dev_ptr_1);
  }
}
