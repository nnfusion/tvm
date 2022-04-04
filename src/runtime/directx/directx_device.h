#pragma once

#include "directx_buffer.h"
#include "directx_header.h"
#include "directx_kernel.h"

#ifdef __linux__
//Those for linux/wsl
#include <sys/eventfd.h>
#endif

namespace tvm {
namespace runtime {
namespace dx {

class DirectXDevice {
 public:
  DirectXDevice(ComPtr<IDXCoreAdapter> adapter, uint32_t device_number) {
    const D3D_FEATURE_LEVEL dx_min_lv{D3D_FEATURE_LEVEL_12_0};
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), dx_min_lv, IID_PPV_ARGS(&_dev)));
    _cmd_alloc = create_cmd_alloc();
    _cmd_queue = create_cmd_queue();
    _number = device_number;
    _frame = 0;
    _adapter = adapter;
  }

  // This is to get device's driver name, usually the name of the device.
  std::string get_driver_description() {
    auto rs = get_property(DXCoreAdapterProperty::DriverDescription);
    std::string name(rs.data());
    return name;
  }

  // Unsafe type trans
  uint64_t get_dedicated_memorysize() {
    auto rs = get_property(DXCoreAdapterProperty::DedicatedAdapterMemory);
    if (rs.size() == 8) {
      auto p = (uint64_t*)(rs.data());
      return *p;
    }
    return 0;
  }

  uint32_t get_device_number() { return _number; }

  bool get_is_hardware() {
    auto rs = get_property(DXCoreAdapterProperty::IsHardware);
    if (rs.size() == 1) return rs[0];
    return false;
  }

  bool get_is_integrated() {
    auto rs = get_property(DXCoreAdapterProperty::IsIntegrated);
    if (rs.size() == 1) return rs[0];
    return false;
  }

  bool get_is_detachable() {
    auto rs = get_property(DXCoreAdapterProperty::IsDetachable);
    if (rs.size() == 1) return rs[0];
    return false;
  }

  std::string debug_str() {
    std::stringstream ss;
    ss << get_device_number() << "\t" << get_driver_description() << "\t"
       << "DedicatedMemory: " << get_dedicated_memorysize() << "B\t"
       << "Hardware: " << (get_is_hardware() ? "True" : "False") << "\t"
       << "Integrated: " << (get_is_integrated() ? "True" : "False") << "\t"
       << "Detachable: " << (get_is_detachable() ? "True" : "False");
    return ss.str();
  }

  // Command Queue sends signal when finished
  // Fence waits the singal then trigger event
  // CPU is waiting for the event
  void device_fence_sync() {
#ifdef _WIN32
    // Create fence which has frame#0
    ComPtr<ID3D12Fence> fence;
    _dev->CreateFence(_frame, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence));
    // Let fench wait for frame + 1
    InterlockedIncrement(&_frame);

    if (fence == nullptr) throw std::runtime_error(_msg_("Cannot create fence."));
    auto event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (event == NULL) throw std::runtime_error(_msg_("Event created failed."));
    // Wait the sign
    ThrowIfFailed(_cmd_queue->Signal(fence.Get(), _frame));
    ThrowIfFailed(fence->SetEventOnCompletion(_frame, event));
    auto rv = WaitForSingleObject(event, INFINITE);
    if (rv != WAIT_OBJECT_0) throw std::runtime_error(_msg_("Fence error."));
#elif __linux__
    // Create fence which has frame#0
    ComPtr<ID3D12Fence> fence;
    _dev->CreateFence(_frame, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence));
    // Let fench wait for frame + 1
    _frame++;

    if (fence == nullptr) throw std::runtime_error(_msg_("Cannot create fence."));
    auto event = eventfd(0, 0);
    if (event < 0) throw std::runtime_error(_msg_("Event created failed."));
    // Wait the sign
    ThrowIfFailed(_cmd_queue->Signal(fence.Get(), _frame));
    ThrowIfFailed(fence->SetEventOnCompletion(_frame, reinterpret_cast<HANDLE>(event)));
    eventfd_t count;
    auto rv = eventfd_read(event, &count);
    if (rv < 0) throw std::runtime_error(_msg_("Fence error."));
#endif
  }

  std::shared_ptr<DirectDeviceBuffer> device_buffer(UINT64 size, DLDataType type) {
    return std::make_shared<DirectDeviceBuffer>(this, size, type);
  }
  std::shared_ptr<DirectHostBuffer> host_buffer(
      UINT64 size, DLDataType type,
      DirectHostBuffer::hostbuffer_state state = DirectHostBuffer::hostbuffer_state::upload) {
    return std::make_shared<DirectHostBuffer>(this, size, type, state);
  }
  DirectUploadBuffer upload_buffer(UINT64 size, DLDataType type) {
    return DirectUploadBuffer(this, size, type);
  }
  DirectReadBackBuffer readback_buffer(UINT64 size, DLDataType type) {
    return DirectReadBackBuffer(this, size, type);
  }

  void buffer_copy(std::shared_ptr<DirectBuffer> tgt, std::shared_ptr<DirectBuffer> src,
                   bool async = false) {
    // Device -> Device
    if (tgt->is_device() && src->is_device()) {
      copy(tgt->device(), src->device(), async);
    } else
      // Device -> Host
      if (tgt->is_host() && src->is_device()) {
        // if change some part, otherwise just reallocate a new buff;
        std::static_pointer_cast<DirectHostBuffer>(tgt)->change_state(
            DirectHostBuffer::hostbuffer_state::readback);
        copy(tgt->host(), src->device(), async);
      } else
        // Host -> Device
        if (tgt->is_device() && src->is_host()) {
          // if change some part, otherwise just reallocate a new buff;
          std::static_pointer_cast<DirectHostBuffer>(src)->change_state(
              DirectHostBuffer::hostbuffer_state::upload);
          copy(tgt->device(), src->host(), async);
        } else if (tgt->is_host() && src->is_host()) {
          // if change some part, otherwise just reallocate a new buff;
          std::static_pointer_cast<DirectHostBuffer>(src)->change_state(
              DirectHostBuffer::hostbuffer_state::upload);
          std::static_pointer_cast<DirectHostBuffer>(tgt)->change_state(
              DirectHostBuffer::hostbuffer_state::readback);
          copy(tgt->host(), src->host(), async);
        }
  }

  void buffer_copy(std::shared_ptr<DirectBuffer> tgt, UINT64 tgt_offset,
                   std::shared_ptr<DirectBuffer> src, UINT64 src_offset, UINT64 num_bytes,
                   bool async = false) {
    // Device -> Device
    if (tgt->is_device() && src->is_device()) {
      range_copy(tgt->device(), tgt_offset, src->device(), src_offset, num_bytes, async);
    } else
      // Device -> Host
      if (tgt->is_host() && src->is_device()) {
        // if change some part, otherwise just reallocate a new buff;
        std::static_pointer_cast<DirectHostBuffer>(tgt)->change_state(
            DirectHostBuffer::hostbuffer_state::readback);
        range_copy(tgt->host(), tgt_offset, src->device(), src_offset, num_bytes, async);
      } else
        // Host -> Device
        if (tgt->is_device() && src->is_host()) {
          // if change some part, otherwise just reallocate a new buff;
          std::static_pointer_cast<DirectHostBuffer>(src)->change_state(
              DirectHostBuffer::hostbuffer_state::upload);
          range_copy(tgt->device(), tgt_offset, src->host(), src_offset, num_bytes, async);
        } else
          // Host -> Host
          if (tgt->is_host() && src->is_host()) {
            // if change some part, otherwise just reallocate a new buff;
            std::static_pointer_cast<DirectHostBuffer>(src)->change_state(
                DirectHostBuffer::hostbuffer_state::upload);
            std::static_pointer_cast<DirectHostBuffer>(tgt)->change_state(
                DirectHostBuffer::hostbuffer_state::readback);
            range_copy(tgt->host(), tgt_offset, src->host(), src_offset, num_bytes, async);
          }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DirectXDevice);
  friend class DirectComputeKernel;
  friend class DirectBuffer;
  friend class DirectReadBackBuffer;
  friend class DirectUploadBuffer;
  friend class DirectHostBuffer;
  friend class DirectDeviceBuffer;
  UINT64 _frame;
  uint32_t _number;
  ComPtr<ID3D12Device> _dev;
  ComPtr<IDXCoreAdapter> _adapter;
  ComPtr<ID3D12CommandAllocator> _cmd_alloc;
  ComPtr<ID3D12CommandQueue> _cmd_queue;
  std::vector<ComPtr<ID3D12CommandQueue>> _cmd_queue_list;

 private:
  // This it to get proper raw data.
  std::vector<char> get_property(DXCoreAdapterProperty property) {
    if (_adapter->IsPropertySupported(property)) {
      size_t len;
      ThrowIfFailed(_adapter->GetPropertySize(property, &len));
      std::vector<char> buf(len);
      ThrowIfFailed(_adapter->GetProperty(property, len, buf.data()));
      return buf;
    }
    return {};
  }

  bool is_file_existed(const std::string& src) {
    std::wifstream i(src);
    if (!i) return false;
    i.close();
    return true;
  }

  // Different type of memory, and their relationship: x means not working.
  //      ---------------->
  //      |
  //      --->        ---->
  // upload    device     readback
  //       <-x-      <-x-  |
  //     <-------x----------
  //
  // upload & readback are mostly on RAM, device mem is on VRAM.

  // This heap type experiences the most bandwidth for the GPU, but cannot provide CPU access.
  // vram allocated memory: 1. on heap; 2. unordered access;
  ComPtr<ID3D12Resource> device_allocate(uint64_t size) {
    size = ((size - 1) | 1023) + 1;
    ComPtr<ID3D12Resource> vram_data;
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto resourceDesc =
        CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0);
    ThrowIfFailed(_dev->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&vram_data)));
    return vram_data;
  }

  // This heap type has CPU access optimized for uploading to the GPU, but does not experience the
  // maximum amount of bandwidth for the GPU.
  ComPtr<ID3D12Resource> upload_allocate(uint64_t size) {
    size = ((size - 1) | 1023) + 1;
    ComPtr<ID3D12Resource> _data;
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE, 0);
    ThrowIfFailed(_dev->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&_data)));
    return _data;
  }

  // This heap type has CPU access optimized for reading data back from the GPU, but does not
  // experience the maximum amount of bandwidth for the GPU.
  ComPtr<ID3D12Resource> readback_allocate(uint64_t size) {
    size = ((size - 1) | 1023) + 1;
    ComPtr<ID3D12Resource> _data;
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE, 0);
    ThrowIfFailed(_dev->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&_data)));
    return _data;
  }

  ComPtr<ID3D12CommandQueue> create_cmd_queue() {
    ComPtr<ID3D12CommandQueue> _cmd_queue;
    auto desc = D3D12_COMMAND_QUEUE_DESC{D3D12_COMMAND_LIST_TYPE_COMPUTE, 0,
                                         D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    ThrowIfFailed(_dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&_cmd_queue)));
    return _cmd_queue;
  }

  void close_cmd_list(ComPtr<ID3D12GraphicsCommandList1> gcl) { gcl->Close(); }

  void execute_cmd_queue(ComPtr<ID3D12CommandList> cl, bool async = false) {
    ID3D12CommandList* ppCommandLists[] = {cl.Get()};
    _cmd_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    if (!async) device_fence_sync();
  }

  ComPtr<ID3D12CommandAllocator> create_cmd_alloc() {
    ComPtr<ID3D12CommandAllocator> cmd_alloc;
    ThrowIfFailed(
        _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&cmd_alloc)));
    return cmd_alloc;
  }

  // Create a cmd list support both compute and copy
  ComPtr<ID3D12GraphicsCommandList1> create_cmd_list(ID3D12PipelineState* ps = nullptr) {
    ComPtr<ID3D12GraphicsCommandList1> _cmd_list;
    ThrowIfFailed(_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, _cmd_alloc.Get(), ps,
                                          IID_PPV_ARGS(&_cmd_list)));
    return _cmd_list;
  }

  void copy(ComPtr<ID3D12Resource> to, ComPtr<ID3D12Resource> from, bool async = false) {
    auto cl = create_cmd_list();
    cl->CopyResource(to.Get(), from.Get());
    ThrowIfFailed(cl->Close());
    execute_cmd_queue(cl, async);
  }

  void range_copy(ComPtr<ID3D12Resource> to, UINT64 DstOffset, ComPtr<ID3D12Resource> from,
                  UINT64 SrcOffset, UINT64 NumBytes, bool async = false) {
    auto cl = create_cmd_list();
    cl->CopyBufferRegion(to.Get(), DstOffset, from.Get(), SrcOffset, NumBytes);
    ThrowIfFailed(cl->Close());
    execute_cmd_queue(cl, async);
  }

 public:
  std::shared_ptr<DirectComputeKernel> gen_computer_shader_from_string(const std::string src) {
    std::shared_ptr<DirectComputeKernel> ptr(new DirectComputeKernel(src));
    return ptr;
  }

  void dispatch(std::shared_ptr<DirectComputeKernel> shader,
                std::vector<std::shared_ptr<DirectBuffer>> buf, std::string function = "",
                std::vector<uint32_t> block = {}, std::vector<uint32_t> thread = {},
                bool async = false) {
    shader->device_dispatch(this, buf, function, block, thread, async);
  }
};

}  // namespace dx
}  // namespace runtime
}  // namespace tvm
