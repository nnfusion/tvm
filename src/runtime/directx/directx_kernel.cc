#define INITGUID
#include <regex>

#include "directx_header.h"

using namespace tvm::runtime::dx;

void DirectComputeKernel::device_create_launch_state(
    DirectXDevice* _dxdev, std::vector<std::shared_ptr<DirectBuffer>>& _buf,
    ComPtr<ID3DComputerShader>& _kernel, ComPtr<ID3D12PipelineState>& _ps,
    ComPtr<ID3D12RootSignature>& _sig, ComPtr<ID3D12DescriptorHeap>& _heap) {
  ComPtr<ID3DBlob> _sig_blob;
  ComPtr<ID3DBlob> _err_blob;

  // create parameter desc from args
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = _buf.size();
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  // This flag is to make data allocated on GPU
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(_dxdev->_dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_heap)));

  UINT _inc_sz =
      _dxdev->_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto _cpu_hnd = _heap->GetCPUDescriptorHandleForHeapStart();
  // This will create UAV descriptor and Commited Resource on heap
  // Heap{UAV}
  for (size_t i = 0; i < _buf.size(); ++i) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC _uav_desc;
    ZeroMemory(&_uav_desc, sizeof(_uav_desc));
    _uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    _uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    _uav_desc.Buffer.FirstElement = 0;
    _uav_desc.Buffer.NumElements = _buf[i]->size / (_buf[i]->type.bits / 8);
    _uav_desc.Buffer.StructureByteStride = (UINT)(_buf[i]->type.bits / 8);
    _dxdev->_dev->CreateUnorderedAccessView(_buf[i]->device().Get(), nullptr, &_uav_desc, _cpu_hnd);
    _cpu_hnd.ptr += _inc_sz;
  }

  // == Create Root Signature ==
  // 1st, create descriptor range,
  CD3DX12_DESCRIPTOR_RANGE1 _rg[1];
  // UAV = Unordered Access View (could be buffers, textures, ...), support multithread access;
  _rg[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, _buf.size(), 0, 0,
              D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);

  // 2nd, create root parameter,
  CD3DX12_ROOT_PARAMETER1 _rp[1];
  _rp[0].InitAsDescriptorTable(_countof(_rg), _rg);
  // 3rd, create root signature,
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC _sig_desc;
  ZeroMemory(&_sig_desc, sizeof(_sig_desc));
  _sig_desc.Init_1_1(_countof(_rp), _rp);

  // 4st, device create root signature,
  ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                      &_sig_blob, &_err_blob));
  ThrowIfFailed(_dxdev->_dev->CreateRootSignature(0, _sig_blob->GetBufferPointer(),
                                                  _sig_blob->GetBufferSize(), IID_PPV_ARGS(&_sig)));
  // last, creat pipeline state.
  // Computer pipeline state description contains: signature, kernel, etc ...
  D3D12_COMPUTE_PIPELINE_STATE_DESC _cpsd;
  ZeroMemory(&_cpsd, sizeof(_cpsd));
  _cpsd.CS = CD3DX12_SHADER_BYTECODE(_kernel.Get());
  _cpsd.pRootSignature = _sig.Get();
  _cpsd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  // Put description int computer pipeline state
  ThrowIfFailed(_dxdev->_dev->CreateComputePipelineState(&_cpsd, IID_PPV_ARGS(&_ps)));
}

void DirectComputeKernel::device_dispatch(DirectXDevice* _dxdev,
                                          std::vector<std::shared_ptr<DirectBuffer>> _buf,
                                          std::string _func_name, std::vector<uint32_t> block,
                                          std::vector<uint32_t> thread, bool async) {
  if (_func_name.empty()) _func_name = this->_func_info.name;
  if (_dxdev == nullptr)
    throw std::invalid_argument(_msg_("DirectX kernel launch failed: Invalid DirectX device;"));
  if (_func_descs.find(_func_name) == _func_descs.end()) {
    FunctionDescriptor fd;
    fd.name = _func_name;
    fd.block = {1, 1, 1};
    fd.thread = {1, 1, 1};
    _func_descs[_func_name] = fd;
  }
  auto& function = _func_descs[_func_name];
  if (block.empty()) block = function.block;
  if (thread.empty()) thread = function.thread;
  ComPtr<ID3DComputerShader> _kernel = d3d_compile_with_threads(_hlsl_source, _func_name, thread);
  // todo(wenxh): Those states can be merged into some state structure
  device_create_launch_state(_dxdev, _buf, _kernel, function.pipeline_state, function.signature,
                             function.heap);
  // Generate command list from computer pipline state
  auto cl = _dxdev->create_cmd_list(function.pipeline_state.Get());
  // This table is assigned to _heap, which is first element of hps[]
  cl->SetComputeRootSignature(
      function.signature.Get());  // keep this before set heap even though _heap has _sig
  ID3D12DescriptorHeap* hps[] = {function.heap.Get()};
  cl->SetDescriptorHeaps(_countof(hps), hps);
  cl->SetComputeRootDescriptorTable(0, function.heap->GetGPUDescriptorHandleForHeapStart());

  if (block.size() < 3) block.resize(3, 1);
  cl->Dispatch(block[0], block[1], block[2]);
  ThrowIfFailed(cl->Close());
  _dxdev->execute_cmd_queue(cl, async);
}

std::string DirectComputeKernel::read_from_hlsl_file(const std::string& hlsl) {
  std::ifstream fl(hlsl);
  std::istreambuf_iterator<char> begin(fl);
  std::istreambuf_iterator<char> end;
  std::string content(begin, end);
  return content;
}

DirectComputeKernel::DirectComputeKernel(const std::string& src) { _hlsl_source = src; }

ComPtr<ID3DComputerShader> DirectComputeKernel::d3d_compile_with_threads(
    const std::string& src, std::string entry, std::vector<uint32_t> thread) {
  auto& exist_thread = _func_descs[entry].thread;
  auto& desc = _func_descs[entry];
  if (desc.cache.find(thread) != desc.cache.end()) {
    // Return existed cached kernel;
    return desc.cache[thread];
  }
  // Replace [numthreads(1, 1, 1)] with new thread configure;
  // HLSL function: "[numthreads(1, 1, 1)] void inc() { }"
  std::string regstr = "\\[\\s*numthreads\\s*\\(\\s*" + std::to_string(exist_thread[0]) +
                       "\\s*,\\s*" + std::to_string(exist_thread[1]) + "\\s*,\\s*" +
                       std::to_string(exist_thread[2]) + "\\s*\\)\\s*\\]\\s*void\\s*" + desc.name;
  std::regex thread_match(regstr);
  std::string source = src;
  std::string new_threads = "[numthreads\(" + std::to_string(thread[0]) + ", " +
                            std::to_string(thread[1]) + ", " + std::to_string(thread[2]) +
                            "\)] void " + desc.name;
  // LOG(INFO) << "DirectX Runtime Launch kernel: " << new_threads;
  std::string new_source = std::regex_replace(source, thread_match, new_threads);
  ComPtr<ID3DComputerShader> ics;
  d3d_compile(ics, new_source, entry);
  desc.cache[thread] = ics;
  return ics;
}

void DirectComputeKernel::d3d_compile(ComPtr<ID3DComputerShader>& entry_blob,
                                      const std::string& src, std::string entry_point,
                                      std::string profile) {
  /*
  ComPtr<ID3DBlob> error_blob;
  ID3DInclude* dxc_includes = D3D_COMPILE_STANDARD_FILE_INCLUDE;
  UINT dxc_flags = D3DCOMPILE_ENABLE_STRICTNESS;
  std::string fake_file = entry_point + "_kernel.hlsl";
  ThrowIfFailed(D3DCompile(src.c_str(), src.length(), fake_file.c_str(), nullptr, dxc_includes,
                           entry_point.c_str(), profile.c_str(), dxc_flags, 0, &(entry_blob),
                           &error_blob));
  // Error blob mostly contains warning message.
  if (error_blob != nullptr) LOG(INFO) << (char*)error_blob->GetBufferPointer();
  */
}
