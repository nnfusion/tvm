#define INITGUID

#include "directx_header.h"
#include "directx_shader_compiler.h"

using namespace tvm::runtime::dx;

DirectComputeKernel::BufferViewType DirectComputeKernel::GetViewType(
    const D3D12_SHADER_INPUT_BIND_DESC& desc) {
  if ((desc.Dimension != D3D_SRV_DIMENSION_BUFFER) && (desc.Type != D3D_SIT_CBUFFER) &&
      (desc.Type != D3D_SIT_TBUFFER)) {
    throw std::invalid_argument("Only buffers are supported");
  }

  switch (desc.Type) {
    case D3D_SIT_TEXTURE:      // Buffer
    case D3D_SIT_UAV_RWTYPED:  // RWBuffer
    case D3D_SIT_TBUFFER:      // tbuffer
      return DirectComputeKernel::BufferViewType::Typed;

    case D3D_SIT_CBUFFER:                        // cbuffer
    case D3D_SIT_STRUCTURED:                     // StructuredBuffer
    case D3D_SIT_UAV_RWSTRUCTURED:               // RWStructuredBuffer
    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:  // (Append|Consume)StructuredBuffer
      return DirectComputeKernel::BufferViewType::Structured;

    case D3D_SIT_BYTEADDRESS:        // ByteAddresBuffer
    case D3D_SIT_UAV_RWBYTEADDRESS:  // RWByteAddressBuffer
      return DirectComputeKernel::BufferViewType::Raw;

    default:
      throw std::invalid_argument("Shader input type is not supported");
  }
}

D3D12_DESCRIPTOR_RANGE_TYPE DirectComputeKernel::GetDescriptorRangeType(
    const D3D12_SHADER_INPUT_BIND_DESC& desc) {
  if ((desc.Dimension != D3D_SRV_DIMENSION_BUFFER) && (desc.Type != D3D_SIT_CBUFFER) &&
      (desc.Type != D3D_SIT_TBUFFER)) {
    throw std::invalid_argument("Only buffers are supported");
  }

  switch (desc.Type) {
    case D3D_SIT_CBUFFER:  // cbuffer
      return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

    case D3D_SIT_TEXTURE:      // Buffer
    case D3D_SIT_STRUCTURED:   // StructuredBuffer
    case D3D_SIT_BYTEADDRESS:  // ByteAddresBuffer
    case D3D_SIT_TBUFFER:      // tbuffer
      return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    case D3D_SIT_UAV_RWTYPED:                    // RWBuffer
    case D3D_SIT_UAV_RWSTRUCTURED:               // RWStructuredBuffer
    case D3D_SIT_UAV_RWBYTEADDRESS:              // RWByteAddressBuffer
    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:  // (Append|Consume)StructuredBuffer
      return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    default:
      throw std::invalid_argument("Shader input type is not supported");
  }
}

// Reflects descriptor ranges and binding points from the HLSL source.
DirectComputeKernel::BindingData DirectComputeKernel::ReflectBindingData(
    std ::vector<D3D12_SHADER_INPUT_BIND_DESC> shaderInputDescs) {
  std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges;
  std::vector<BindPoint> bindPoints;

  D3D12_DESCRIPTOR_RANGE1 currentRange = {};
  currentRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  uint32_t currentOffsetInDescriptors = 0;

  for (size_t resourceIndex = 0; resourceIndex < shaderInputDescs.size(); resourceIndex++) {
    const auto& shaderInputDesc = shaderInputDescs[resourceIndex];
    auto viewType = GetViewType(shaderInputDesc);
    auto rangeType = GetDescriptorRangeType(shaderInputDesc);
    auto numDescriptors = shaderInputDesc.BindCount;

    bindPoints.push_back(
        {viewType, rangeType, currentOffsetInDescriptors,
         (viewType == DirectComputeKernel::BufferViewType::Structured ? shaderInputDesc.NumSamples
                                                                      : 0),
         shaderInputDesc.Name});
    if (rangeType == currentRange.RangeType &&
        shaderInputDesc.Space == currentRange.RegisterSpace) {
      currentRange.NumDescriptors += numDescriptors;
    } else {
      if (currentRange.NumDescriptors > 0) {
        descriptorRanges.push_back(currentRange);
      }

      currentRange.RangeType = rangeType;
      currentRange.NumDescriptors = numDescriptors;
      currentRange.RegisterSpace = shaderInputDesc.Space;
    }

    currentOffsetInDescriptors += numDescriptors;
  }

  if (currentRange.NumDescriptors > 0) {
    descriptorRanges.push_back(currentRange);
  }

  return std::make_tuple(descriptorRanges, bindPoints);
}

void DirectComputeKernel::CreateRootSignatureAndBindingMap(
    ComPtr<dxc::ID3D12ShaderReflection>& _reflection, std::vector<BindPoint>& _bindpoints,
    ComPtr<ID3DBlob>& rootSignatureBlob) {
  D3D12_SHADER_DESC shaderDesc = {};
  ThrowIfFailed(_reflection->GetDesc(&shaderDesc));

  std::vector<D3D12_SHADER_INPUT_BIND_DESC> shaderInputDescs(shaderDesc.BoundResources);
  for (uint32_t resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; resourceIndex++) {
    ThrowIfFailed(
        _reflection->GetResourceBindingDesc(resourceIndex, &shaderInputDescs[resourceIndex]));
  }

  std::vector<D3D12_ROOT_PARAMETER1> rootParameters;
  std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges;
  std::tie(descriptorRanges, _bindpoints) = ReflectBindingData(shaderInputDescs);

  if (!descriptorRanges.empty()) {
    D3D12_ROOT_PARAMETER1 rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRanges.size());
    rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges.data();
    rootParameters.push_back(rootParameter);
  }

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
  rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  rootSigDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParameters.size());
  rootSigDesc.Desc_1_1.pParameters = rootParameters.data();

  ComPtr<ID3DBlob> rootSignatureErrors;
  HRESULT hr =
      D3D12SerializeVersionedRootSignature(&rootSigDesc, &rootSignatureBlob, &rootSignatureErrors);
  if (FAILED(hr)) {
    if (rootSignatureErrors) {
      LOG(ERROR) << (static_cast<LPCSTR>(rootSignatureErrors->GetBufferPointer()));
    }
    ThrowIfFailed(hr);
  }
}

/*static*/ DXGI_FORMAT GetDxgiFormatFromDmlTensorDataType(DLDataType dataType) {
  switch (dataType.code) {
    case DLDataTypeCode::kDLFloat:
      return DXGI_FORMAT_R32_FLOAT;
    case DLDataTypeCode::kDLInt:
      return DXGI_FORMAT_R32_UINT;
    default:
      throw std::invalid_argument(_msg_("No DXGI_FORMAT exists for given data type."));
  }
}

void DirectComputeKernel::binding_buffers(DirectXDevice* _dxdev, ComPtr<ID3D12DescriptorHeap> _heap,
                                          const std::vector<BindPoint>& _bindpoints,
                                          const std::vector<std::shared_ptr<DirectBuffer>>& _buf) {
  uint32_t descriptorIncrementSize =
      _dxdev->_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  for (auto& bindPoint : _bindpoints) {
    auto offset = bindPoint.offsetInDescriptorsFromTableStart;
    auto source = _buf[offset]->device();
    auto FirstElement = 0;
    auto StructureByteStride = bindPoint.structureByteStride;
    auto NumElements = _buf[offset]->size / StructureByteStride;
    auto SizeInBytes = _buf[offset]->size;
    auto dt = _buf[offset]->type;

    auto FillViewDesc = [&](auto& viewDesc) {
      viewDesc.Buffer.StructureByteStride = StructureByteStride;
      viewDesc.Buffer.NumElements = NumElements;
      viewDesc.Buffer.FirstElement = FirstElement;

      if (bindPoint.viewType == BufferViewType::Typed) {
        viewDesc.Format = GetDxgiFormatFromDmlTensorDataType(dt);
      } else if (bindPoint.viewType == BufferViewType::Structured) {
        viewDesc.Format = DXGI_FORMAT_UNKNOWN;
      } else if (bindPoint.viewType == BufferViewType::Raw) {
        viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        if constexpr (std::is_same<decltype(viewDesc), D3D12_UNORDERED_ACCESS_VIEW_DESC&>::value) {
          viewDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        if constexpr (std::is_same<decltype(viewDesc), D3D12_SHADER_RESOURCE_VIEW_DESC&>::value) {
          viewDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        }
      }
    };

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle{
        _heap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<int>(bindPoint.offsetInDescriptorsFromTableStart), descriptorIncrementSize};

    if (bindPoint.descriptorType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      FillViewDesc(uavDesc);
      // todo(wenxh): To support counter resource
      _dxdev->_dev->CreateUnorderedAccessView(source.Get(), nullptr, &uavDesc, cpuHandle);
    } else if (bindPoint.descriptorType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      FillViewDesc(srvDesc);
      _dxdev->_dev->CreateShaderResourceView(source.Get(), &srvDesc, cpuHandle);
    } else if (bindPoint.descriptorType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
      if (SizeInBytes % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT != 0) {
        throw std::invalid_argument(_msg_(
            "Attempting to bind " + bindPoint.descName + " as a constant buffer, but its size (" +
            std::to_string(SizeInBytes) + " bytes) is not aligned to " +
            std::to_string(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) + " bytes"));
      }

      D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
      cbvDesc.BufferLocation = source.Get()->GetGPUVirtualAddress();
      cbvDesc.SizeInBytes = SizeInBytes;
      _dxdev->_dev->CreateConstantBufferView(&cbvDesc, cpuHandle);
    } else {
      throw std::invalid_argument("Unexpected binding type");
    }
  }
}

void DirectComputeKernel::device_dispatch(DirectXDevice* _dxdev,
                                          std::vector<std::shared_ptr<DirectBuffer>> _buf,
                                          std::string _func_name, std::vector<uint32_t> block,
                                          std::vector<uint32_t> thread, bool async) {
  // One: Get dxil kernel along with irregular cases
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
  ComPtr<dxc::IDxcBlob> _kernel = dxc_compile_with_threads(_hlsl_source, _func_name, thread);

  // Two: Get Signature and Pipeline State
  if (function.signature == nullptr) {
    ThrowIfFailed(_dxdev->_dev->CreateRootSignature(
        0, this->_rootsig_blob->GetBufferPointer(), this->_rootsig_blob->GetBufferSize(),
        IID_PPV_ARGS(function.signature.ReleaseAndGetAddressOf())));
  }
  if (function.pipeline_state == nullptr) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = function.signature.Get();
    psoDesc.CS.pShaderBytecode = _kernel->GetBufferPointer();
    psoDesc.CS.BytecodeLength = _kernel->GetBufferSize();
    ThrowIfFailed(_dxdev->_dev->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(function.pipeline_state.ReleaseAndGetAddressOf())));
  }

  ComPtr<ID3D12DescriptorHeap> descriptorHeap;
  D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
  descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptorHeapDesc.NumDescriptors = static_cast<uint32_t>(this->_bindpoints.size());
  descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(_dxdev->_dev->CreateDescriptorHeap(
      &descriptorHeapDesc, IID_PPV_ARGS(function.heap.ReleaseAndGetAddressOf())));

  binding_buffers(_dxdev, function.heap, this->_bindpoints, _buf);

  // Three: Generate command list from computer pipline state
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

ComPtr<dxc::IDxcBlob> DirectComputeKernel::dxc_compile_with_threads(const std::string& src,
                                                                    std::string entry,
                                                                    std::vector<uint32_t> thread) {
  auto& exist_thread = _func_descs[entry].thread;
  auto& desc = _func_descs[entry];
  if (desc.kernel_cache.find(thread) != desc.kernel_cache.end()) {
    // Return existed cached kernel;
    return desc.kernel_cache[thread];
  }
  // todo(wenxh): maybe dx will give us new interface to point block config
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
  ComPtr<dxc::IDxcBlob> ics;
  dxc_compile(ics, this->_reflection, new_source, entry);
  desc.kernel_cache[thread] = ics;
  CreateRootSignatureAndBindingMap(this->_reflection, this->_bindpoints, this->_rootsig_blob);
  return ics;
}

void DirectComputeKernel::dxc_compile(ComPtr<dxc::IDxcBlob>& entry_blob,
                                      ComPtr<dxc::ID3D12ShaderReflection>& reflection_blob,
                                      const std::string& src, std::string entry_point,
                                      std::string profile) {
  dxc::dxc_compile(src, entry_point, profile, (void**)(entry_blob.ReleaseAndGetAddressOf()),
                   (void**)(reflection_blob.ReleaseAndGetAddressOf()));
  ThrowIfFailed(dxc::check_dxil(entry_blob->GetBufferPointer()));
}