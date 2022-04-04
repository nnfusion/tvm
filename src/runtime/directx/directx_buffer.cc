#include "directx_header.h"

using namespace tvm::runtime::dx;

DirectBuffer::DirectBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type) : _dxdev(_dev) {
  _res = _dev->device_allocate(size);
  D3D12_RESOURCE_DESC desc = _res->GetDesc();
  this->size = desc.Width;
  this->type = type;
}

DirectBuffer::DirectBuffer(DirectXDevice* _dev, ComPtr<ID3D12Resource> res, DLDataType type)
    : _dxdev(_dev), _res(res) {
  D3D12_RESOURCE_DESC desc = _res->GetDesc();
  this->size = desc.Width;
  this->type = type;
}

DirectHostBuffer::DirectHostBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type, hostbuffer_state state)
    : _cur_state(state), ptr(nullptr) {
  if (state == hostbuffer_state::upload)
    _host_res = _dev->upload_allocate(size);
  else if (state == hostbuffer_state::readback)
    _host_res = _dev->readback_allocate(size);
  else
    throw std::invalid_argument(_msg_("Buffer state is not supported"));
  D3D12_RESOURCE_DESC desc = _host_res->GetDesc();
  this->size = desc.Width;
  range = {0, static_cast<SIZE_T>(size)};
  _dxdev = _dev;
  this->type = type;
}

void* DirectHostBuffer::open_data_ptr() {
  if (ptr != nullptr) return ptr;
  ThrowIfFailed(_host_res->Map(0, &range, reinterpret_cast<void**>(&ptr)));
  return ptr;
}

void DirectHostBuffer::close_data_ptr() {
  ptr = nullptr;
  if (_cur_state == hostbuffer_state::readback)
  // Use begin = end to tell no data is changed;
  {
    D3D12_RANGE range = {0, 0};
    _host_res->Unmap(0, &range);
  } else
    _host_res->Unmap(0, &range);
}

// todo(wenxh): use resource barrier to support call this transition in async way;
void DirectHostBuffer::change_state(hostbuffer_state hs) {
  if (hs == _cur_state) return;
  D3D12_RESOURCE_DESC desc = _host_res->GetDesc();
  if (hs == hostbuffer_state::upload) {
    // readback to upload, need to memcpy
    auto tgt = _dxdev->upload_allocate(size);
    // cpu memcpy
    {
      // open ptr
      void* t_ptr = nullptr;
      ThrowIfFailed(tgt->Map(0, &range, reinterpret_cast<void**>(&t_ptr)));
      open_data_ptr();
      memcpy(t_ptr, ptr, size);

      // close ptr
      close_data_ptr();
      tgt->Unmap(0, &range);
    }

    _host_res = tgt;
  } else if (hs == hostbuffer_state::readback) {
    close_data_ptr();
    auto tgt = _dxdev->readback_allocate(size);
    _dxdev->copy(tgt, _host_res);
    _host_res = tgt;
  } else {
    throw std::invalid_argument(_msg_("Target buffer state is not supported."));
  }
  _cur_state = hs;
}

DirectReadBackBuffer::DirectReadBackBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type)
    : DirectBuffer(_dev, size, type) {
  _host_res = _dev->readback_allocate(size);
  range = {0, static_cast<SIZE_T>(size)};
  this->ptr = nullptr;
}

void* DirectReadBackBuffer::open_data_ptr() {
  if (ptr != nullptr) return ptr;
  ThrowIfFailed(_host_res->Map(0, &range, reinterpret_cast<void**>(&ptr)));
  return ptr;
}

void DirectReadBackBuffer::to_host(bool async) { _dxdev->copy(_host_res, _res, async); }

void DirectReadBackBuffer::close_data_ptr() {
  ptr = nullptr;
  // Use begin = end to tell no data is changed;
  D3D12_RANGE range = {0, 0};
  _host_res->Unmap(0, &range);
}

DirectUploadBuffer::DirectUploadBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type)
    : DirectBuffer(_dev, size, type) {
  _host_res = _dev->upload_allocate(size);
  range = {0, static_cast<SIZE_T>(size)};
  this->ptr = nullptr;
}

void* DirectUploadBuffer::open_data_ptr() {
  if (ptr != nullptr) return ptr;
  ThrowIfFailed(_host_res->Map(0, &range, reinterpret_cast<void**>(&ptr)));
  return ptr;
}

void DirectUploadBuffer::close_data_ptr() {
  ptr = nullptr;
  _host_res->Unmap(0, &range);
}

void DirectUploadBuffer::to_device(bool async) { _dxdev->copy(_res, _host_res, async); }
