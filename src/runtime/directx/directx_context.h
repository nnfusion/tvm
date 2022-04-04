#pragma once

#include "directx_header.h"

namespace tvm {
namespace runtime {
namespace dx {

// DirectXContext will:
// 1. Find all supported video adapters;
// 2. Invoke DirectXDevice class to create D3DDevice using adapter;
class DirectXContext {
 public:
  void update_devices() {
    // Enable experimental features on device:
    //  - Such as running dxil without signed, which needs Windows being in Developer mode
    //  - URL: https://github.com/microsoft/DirectXShaderCompiler/wiki/Running-Shaders
    ThrowIfFailed(
        D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, nullptr, nullptr));

    _devices.clear();
    // Get all qualified devices;
    ComPtr<IDXCoreAdapterFactory> d3d_adapter_factory;
    ComPtr<IDXCoreAdapterList> d3d_adapter_list;
    GUID dx_must_attr[1]{DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE};
    bool dx_must_hardware{true};
    ThrowIfFailed(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&d3d_adapter_factory)));
    ThrowIfFailed(d3d_adapter_factory->CreateAdapterList(_countof(dx_must_attr), dx_must_attr,
                                                         IID_PPV_ARGS(&d3d_adapter_list)));
    const uint32_t count{d3d_adapter_list->GetAdapterCount()};

    // Generate all devices object;
    for (uint32_t i = 0; i < count; i++) {
      ComPtr<IDXCoreAdapter> d3d_adapter;
      ThrowIfFailed(d3d_adapter_list->GetAdapter(i, IID_PPV_ARGS(&d3d_adapter)));
      try {
        auto pdxdev = std::make_unique<DirectXDevice>(d3d_adapter, _devices.size());
        _devices.push_back(std::move(pdxdev));
      } catch (com_exception e) {
        LOG(INFO) << "DirectX Adapter #" << i << " not supported: cannot create device.";
        throw e;
      }
    }
  }

  // This will get a reference of numbered device;
  std::shared_ptr<DirectXDevice> get_device(uint32_t device_number = 0) {
    if (device_number >= _devices.size())
      if (_devices.empty())
        throw std::range_error(_msg_("No DirectX device found."));
      else
        throw std::range_error(_msg_("Try to aquired unexisted DirectX devices."));
    return _devices[device_number];
  }

  // This is used to enumate all possible devices;
  uint32_t get_devices_count() { return _devices.size(); }

  void enable_debug_layer() {
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DirectXContext);
  std::vector<std::shared_ptr<DirectXDevice>> _devices;
  ComPtr<ID3D12Debug> debugController;

 public:
  DirectXContext() {
    // enable_debug_layer();
    update_devices();
  }
};

}  // namespace dx
}  // namespace runtime
}  // namespace tvm
