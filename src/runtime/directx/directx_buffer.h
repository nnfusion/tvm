#pragma once
#include "directx_header.h"

//todo(wenxh): To remove DirectUpload/ReadBackBuffer due to not useful to TVM Runtime;

namespace tvm {
namespace runtime {
namespace dx {

// D3DResource -> map to void* for write/read -> unmap
class DirectBuffer {
 public:
  virtual ComPtr<ID3D12Resource> device() { return _res; }
  virtual ComPtr<ID3D12Resource> host() { throw std::runtime_error(_msg_("No host resource found.")); }
  virtual void* open_data_ptr() { throw std::runtime_error(_msg_("No support.")); }
  virtual void close_data_ptr() { throw std::runtime_error(_msg_("No support.")); }
  virtual void to_host(bool async = false) { throw std::runtime_error(_msg_("No support.")); }
  virtual void to_device(bool async = false) { throw std::runtime_error(_msg_("No support.")); }
  virtual bool is_data_ptr_open() { return false; }
  virtual bool is_host() { return false; }
  virtual bool is_device() { return true; }

  DirectBuffer(DirectXDevice* _dev, ComPtr<ID3D12Resource> res, DLDataType type);
  DirectBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type);
  DirectBuffer() {
    _dxdev = nullptr;
    size = 0;
    type = DLDataType{kDLFloat, 32, 1};

  }

  friend class DirectXDevice;
  DirectXDevice* _dxdev;
  ComPtr<ID3D12Resource> _res;
  UINT64 size;
  DLDataType type;
};

class DirectDeviceBuffer final : public DirectBuffer {
 public:
  DirectDeviceBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type) : DirectBuffer(_dev, size, type) {}
  bool is_data_ptr_open() override { return false; }
};

// This is the interface to hide whether the buffer is upload or readback
class DirectHostBuffer final : public DirectBuffer {
 public:
  enum class hostbuffer_state { upload, readback };

  virtual ComPtr<ID3D12Resource> device() { throw std::runtime_error(_msg_("No host resource found.")); }
  ComPtr<ID3D12Resource> host() override { return _host_res; };
  void* open_data_ptr() override;
  void close_data_ptr() override;
  bool is_data_ptr_open() override { return ptr != nullptr; }
  void change_state(hostbuffer_state hs);
  DirectHostBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type, hostbuffer_state state);
  bool is_host() override { return true; }
  bool is_device() override { return false; }


 private:
  friend class DirectXDevice;
  ComPtr<ID3D12Resource> _host_res;
  ComPtr<ID3D12Resource> _host_tmp_res;
  D3D12_RANGE range;
  void* ptr;
  hostbuffer_state _cur_state;
};

class DirectReadBackBuffer : public DirectBuffer {
 public:
  ComPtr<ID3D12Resource> host() override { return _host_res; }
  void* open_data_ptr() override;
  void to_host(bool async = false) override;
  void close_data_ptr() override;
  bool is_data_ptr_open() override { return ptr != nullptr; }
  bool is_host() override { return true; }
  bool is_device() override { return true; }

  DirectReadBackBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type);

 private:
  friend class DirectXDevice;
  ComPtr<ID3D12Resource> _host_res;
  D3D12_RANGE range;
  void* ptr;
};

class DirectUploadBuffer : public DirectBuffer {
 public:
  ComPtr<ID3D12Resource> host() override { return _host_res; }
  void* open_data_ptr() override;
  void close_data_ptr() override;
  void to_device(bool async) override;
  bool is_data_ptr_open() override { return ptr != nullptr; }
  bool is_host() override { return true; }
  bool is_device() override { return true; }

  DirectUploadBuffer(DirectXDevice* _dev, UINT64 size, DLDataType type);

 private:
  friend class DirectXDevice;
  ComPtr<ID3D12Resource> _host_res;
  D3D12_RANGE range;
  void* ptr;
};

}  // namespace dx
}  // namespace runtime
}  // namespace tvm