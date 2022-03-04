/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file directx_workspace.h
 */

#include "directx_common.h"

#ifndef TVM_RUNTIME_DIRECTX_DIRECTX_WORKSPACE_H_
#define TVM_RUNTIME_DIRECTX_DIRECTX_WORKSPACE_H_

namespace tvm {
namespace runtime {
namespace dx {

union DeviceExt {
  Device device;
  struct Ext {
    int device_type;
    int device_id;
  } ext;
};

inline uint32_t dev_to_uint32(tvm::Device dev) { return dev.device_id * 100 + dev.device_type; }
inline Device create_device(int device_type, int device_id) {
  DeviceExt de;
  de.ext.device_type = device_type;
  de.ext.device_id = device_id;
  return de.device;
}

/*!
 * \brief DirectX Workspace
 */
class DirectXWorkspace final : public DeviceAPI {
 private:
  // Init and get devices
  std::unique_ptr<DirectXContext> dx_context{nullptr};
  // Current default device
  std::shared_ptr<DirectXDevice> dx_device{nullptr};
  Device _device;
  // Whether it is initialized.
  bool initialized_{false};
  // the mutex for initialization
  std::mutex mutex;
  // data structure for each device
  struct DeviceState {
    std::shared_ptr<DirectXDevice> device{nullptr};
    std::map<int64_t, std::shared_ptr<DirectBuffer>> buffer;
  };
  std::unordered_map<uint32_t, DeviceState> device_state;

 public:
  // Destructor
  DirectXWorkspace();
  ~DirectXWorkspace(){};
  std::shared_ptr<DirectXDevice> GetDefaultDirectXDevice() { return this->dx_device; }
  Device GetDefaultDevice() { return _device; }
  std::shared_ptr<DirectXDevice> GetDevice(Device dev) {
    if (static_cast<int>(dev.device_type) == kDLDirectX &&
        dev.device_id < dx_context->get_devices_count())
      return dx_context->get_device(dev.device_id);
    else
      throw std::runtime_error("Access invalid DirectX device");
  }
  // override device API
  void SetDevice(Device dev) final;
  void GetAttr(Device dev, DeviceAttrKind kind, TVMRetValue* rv) final;
  void* AllocDataSpace(Device dev, size_t nbytes, size_t alignment = 0,
                       DLDataType type_hint = {}) final;
  void FreeDataSpace(Device dev, void* ptr) final;
  std::shared_ptr<DirectBuffer> RetrieveDataSpace(Device dev, void* ptr) {
    return device_state[dev_to_uint32(dev)].buffer[(uint64_t)ptr];
  }
  // Stream related
  TVMStreamHandle CreateStream(Device dev) final;
  void FreeStream(Device dev, TVMStreamHandle stream) final;
  void StreamSync(Device dev, TVMStreamHandle stream) final;
  void SetStream(Device dev, TVMStreamHandle stream) final;
  void ReinitializeStreams();
  void SyncStreamFromTo(Device dev, TVMStreamHandle event_src, TVMStreamHandle event_dst) final {}
  // Space
  void* AllocWorkspace(Device dev, size_t size, DLDataType type_hint = {}) final;
  void FreeWorkspace(Device dev, void* data) final;
  // get the global workspace
  static DirectXWorkspace* Global();
  void CopyDataFromTo(const void* from, size_t from_size, void* to, size_t to_size, size_t size,
                      Device dev_from, Device dev_to, DLDataType type_hint = {},
                      TVMStreamHandle stream = nullptr) final;
};

}  // namespace dx
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_DIRECTX_DIRECTX_WORKSPACE_H_
