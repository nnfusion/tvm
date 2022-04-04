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

#include "directx_common.h"

using namespace tvm::runtime::dx;

DirectXWorkspace::DirectXWorkspace() {
  if (initialized_ == false) {
    initialized_ = true;
    dx_context = std::make_unique<DirectXContext>();
    SetDevice(create_device(kDLDirectX, 0));
  }
}

void DirectXWorkspace::SetDevice(Device dev) {
  if (static_cast<int>(dev.device_type) == kDLDirectX &&
      dev.device_id < dx_context->get_devices_count()) {
    this->dx_device = dx_context->get_device(dev.device_id);
    _device = dev;
  } else
    throw std::runtime_error(_msg_("Access invalid DirectX device"));
}

void DirectXWorkspace::GetAttr(Device dev, DeviceAttrKind kind, TVMRetValue* rv) {
  std::string str;
  bool f;
  int i;
  switch (kind) {
    case kExist:
      f = true;
      try {
        this->dx_context->get_device(dev.device_id)->get_driver_description();
      } catch (std::exception e) {
        f = false;
      }
      *rv = f;
      return;
    case kMaxThreadsPerBlock:
      *rv = 1024;  // From SM5.0, z-dim is at max of 64
      return;
    case kWarpSize:
      *rv = 16;  // Current hardware mainstream value
      return;
    case kMaxSharedMemoryPerBlock:
      *rv = 32 * 1024;  // D3D12 is 32KB
      return;
    case kComputeVersion:
      *rv = std::string("Microsoft DirectX 12, Shader Model 5.0, Compute Shader");
      return;
    case kDeviceName:
      str = this->dx_context->get_device(dev.device_id)->get_driver_description();
      *rv = std::move(str);
      return;
    case kMaxClockRate:
      return;
    case kMultiProcessorCount:
      return;
    case kMaxThreadDimensions:
      return;
    case kMaxRegistersPerBlock:
      return;
    case kGcnArch:
      return;
    case kApiVersion:
      *rv = 5;
      return;
    case kDriverVersion:
      return;
  }
}

// Alignment and type_hint not works in DirectX, better use runtime default
void* DirectXWorkspace::AllocDataSpace(Device dev, size_t nbytes, size_t alignment,
                                       DLDataType type_hint) {
  uint64_t handle = 0;
  // Allocate host(RAM) memory buffer
  if (static_cast<int>(dev.device_type) == kDLDirectXHost ||
      static_cast<int>(dev.device_type) == kDLDirectXUpload) {
    auto buff = this->dx_device->host_buffer(nbytes, type_hint, DirectHostBuffer::hostbuffer_state::upload);
    handle = (uint64_t)buff.get();
    device_state[dev_to_uint32(dev)].buffer[handle] = buff;
  } else if (static_cast<int>(dev.device_type) == kDLDirectXReadback) {
    auto buff = this->dx_device->host_buffer(nbytes, type_hint, DirectHostBuffer::hostbuffer_state::readback);
    handle = (uint64_t)buff.get();
    device_state[dev_to_uint32(dev)].buffer[handle] = buff;
  } else if (static_cast<int>(dev.device_type) == kDLDirectX) {
    // Allocate VRAM buffer
    auto buff = this->dx_context->get_device(dev.device_id)->device_buffer(nbytes, type_hint);
    handle = (uint64_t)buff.get();
    device_state[dev_to_uint32(dev)].buffer[handle] = buff;
  } else if (dev.device_type = kDLCPU) {
    handle = (uint64_t)malloc(nbytes);
  } else
    throw std::invalid_argument(_msg_("Target buffer state is not supported."));
  // this is a trick: return the offet as void*
  return (void*)(handle);
}

void DirectXWorkspace::FreeDataSpace(Device dev, void* ptr) {
  if (dev.device_type == kDLCPU) free(ptr);
  auto buf_id = (uint64_t)ptr;
  device_state[dev_to_uint32(dev)].buffer.erase(buf_id);
}

void DirectXWorkspace::CopyDataFromTo(const void* from, size_t from_offset, void* to,
                                      size_t to_offset, size_t size, Device dev_from, Device dev_to,
                                      DLDataType type_hint, TVMStreamHandle stream) {
  // FROM: CPU
  if (dev_from.device_type == kDLCPU) {
    if (static_cast<int>(dev_to.device_type) == kDLDirectXHost ||
        static_cast<int>(dev_to.device_type) == kDLDirectXUpload ||
        static_cast<int>(dev_to.device_type) == kDLDirectXReadback) {
      auto to_buffer = device_state[dev_to_uint32(dev_to)].buffer[(uint64_t)to];
      std::static_pointer_cast<DirectHostBuffer>(to_buffer)->change_state(
          DirectHostBuffer::hostbuffer_state::upload);
      // TO: DirectX Host/Upload
      if (to_buffer->is_host()) {
        void* t_ptr = to_buffer->open_data_ptr();
        memcpy((void*)((char*)t_ptr + to_offset), (void*)((char*)from + from_offset), size);
        to_buffer->close_data_ptr();
      } else
        throw std::runtime_error(_msg_("Buffer is not host type."));
    } else
        // TO: DirectX Device
        if (static_cast<int>(dev_to.device_type) == kDLDirectX &&
            dev_to.device_id == _device.device_id) {
      // create a host buffer and copy data
      auto upload_dev = create_device(kDLDirectXUpload, 0);
      std::to_string(upload_dev.device_id);
      auto to_buffer = device_state[dev_to_uint32(_device)].buffer[(uint64_t)to];
      auto total_size = to_buffer->size;
      auto tmp_buffer = AllocDataSpace(upload_dev, total_size);
      CopyDataFromTo(from, from_offset, tmp_buffer, to_offset, size, dev_from, upload_dev,
                     type_hint, stream);
      CopyDataFromTo(tmp_buffer, to_offset, to, to_offset, size, upload_dev, dev_to, type_hint,
                     stream);
      FreeDataSpace(upload_dev, tmp_buffer);
    } else
      throw std::runtime_error(_msg_("Try to access invalid DirectX device(") +
                               std::to_string(static_cast<int>(dev_to.device_type)) + ":" +
                               std::to_string(dev_to.device_id));
  } else
      // TO: CPU
      if (dev_to.device_type == kDLCPU) {
    // FROM: DirectX Host/Readback
    if (static_cast<int>(dev_from.device_type) == kDLDirectXHost ||
        static_cast<int>(dev_from.device_type) == kDLDirectXReadback ||
        static_cast<int>(dev_from.device_type) == kDLDirectXUpload) {
      auto from_buffer = device_state[dev_to_uint32(dev_from)].buffer[(uint64_t)from];
      std::static_pointer_cast<DirectHostBuffer>(from_buffer)
          ->change_state(DirectHostBuffer::hostbuffer_state::readback);
      if (from_buffer->is_host()) {
        void* t_ptr = from_buffer->open_data_ptr();
        memcpy((void*)((char*)to + to_offset), (void*)((char*)t_ptr + from_offset), size);
        from_buffer->close_data_ptr();
      } else
        throw std::runtime_error(_msg_("Buffer is not host type."));
    } else  // FROM: DirectX Device
        if (static_cast<int>(dev_from.device_type) == kDLDirectX &&
            dev_from.device_id == _device.device_id) {
      // create a host buffer and copy data
      auto readback_dev = create_device(kDLDirectXReadback, 0);
      auto from_buffer = device_state[dev_to_uint32(_device)].buffer[(uint64_t)from];
      auto total_size = from_buffer->size;
      auto tmp_buffer = AllocDataSpace(readback_dev, total_size);
      CopyDataFromTo(from, from_offset, tmp_buffer, to_offset, size, dev_from, readback_dev,
                     type_hint, stream);
      CopyDataFromTo(tmp_buffer, to_offset, to, to_offset, size, readback_dev, dev_to, type_hint,
                     stream);
      FreeDataSpace(readback_dev, tmp_buffer);
    } else
      throw std::runtime_error(_msg_("Try to access invalid DirectX device"));
  } else {
    auto from_id = (uint64_t)from;
    auto to_id = (uint64_t)to;
    if (size == 0 && from_offset == 0 && to_offset == 0)
      dx_device->buffer_copy(device_state[dev_to_uint32(dev_to)].buffer[to_id],
                             device_state[dev_to_uint32(dev_from)].buffer[from_id]);
    else
      dx_device->buffer_copy(device_state[dev_to_uint32(dev_to)].buffer[to_id], to_offset,
                             device_state[dev_to_uint32(dev_from)].buffer[from_id], from_offset,
                             size);
  }
}

void* DirectXWorkspace::AllocWorkspace(Device dev, size_t size, DLDataType type_hint) {
  return AllocDataSpace(dev, size, 0, type_hint);
}

void DirectXWorkspace::FreeWorkspace(Device dev, void* data) { FreeDataSpace(dev, data); }

DirectXWorkspace* DirectXWorkspace::Global() {
  static auto* inst = new DirectXWorkspace();
  return inst;
}

TVMStreamHandle DirectXWorkspace::CreateStream(Device dev) { return nullptr; }

void DirectXWorkspace::FreeStream(Device dev, TVMStreamHandle stream) {}

void DirectXWorkspace::StreamSync(Device dev, TVMStreamHandle stream) {}

void DirectXWorkspace::SetStream(Device dev, TVMStreamHandle stream) {}

void DirectXWorkspace::ReinitializeStreams() {}
