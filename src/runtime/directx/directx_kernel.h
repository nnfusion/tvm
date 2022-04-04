#pragma once
#include "../meta_data.h"
#include "directx_header.h"

namespace tvm {
namespace runtime {
namespace dx {

class DirectComputeKernel {
  // Essential data structure type for data exchange
 public:
  struct Func {
    struct Hash {
      // This is from segmentfault & boost
      size_t operator()(const std::vector<uint32_t>& v) const {
        std::hash<uint32_t> hasher;
        size_t seed = 0;
        for (int i : v) {
          seed ^= hasher(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
      }
    };

    std::string name;
    std::vector<uint32_t> block, thread;
    ComPtr<dxc::IDxcBlob> entry;
    std::unordered_map<std::vector<uint32_t>, ComPtr<dxc::IDxcBlob>, Hash> kernel_cache;
    ComPtr<ID3D12PipelineState> pipeline_state;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12DescriptorHeap> heap;
  };
  typedef Func FunctionDescriptor;

 public:
  DirectComputeKernel() {}
  DirectComputeKernel(const std::string& src);
  void set_function_info(FunctionInfo func_info) { _func_info = func_info; }
  void validate_and_sync_buffer(std::vector<std::shared_ptr<DirectBuffer>>& _buf);
  const std::unordered_map<std::string, FunctionDescriptor>& get_function_descriptors() {
    return this->_func_descs;
  }
  const std::string get_entry_function() { return this->_entry_function; };
  static std::string read_from_hlsl_file(const std::string& hlsl);
  static std::string extract_launch_configuration_json(const std::string& ct);
  static void set_launch_configuration_json(const std::string& ct, DirectComputeKernel* _kernel);

 private:
  DISALLOW_COPY_AND_ASSIGN(DirectComputeKernel);
  void device_create_launch_state(DirectXDevice* _dxdev,
                                  std::vector<std::shared_ptr<DirectBuffer>>& _buf,
                                  ComPtr<dxc::IDxcBlob>& _kernel, ComPtr<ID3D12PipelineState>& _ps,
                                  ComPtr<ID3D12RootSignature>& _sig,
                                  ComPtr<ID3D12DescriptorHeap>& _heap);
  void device_dispatch(DirectXDevice* _dxdev, std::vector<std::shared_ptr<DirectBuffer>> _buf,
                       std::string _func_name = "", std::vector<uint32_t> block = {},
                       std::vector<uint32_t> thread = {}, bool async = false);

 private:
  // Those are APIs from DirectML/dxDispatcher project
  enum class BufferViewType {
    Typed,       // (RW)Buffer
    Structured,  // (RW|Append|Consume)StructuredBuffer
    Raw          // (RW)ByteAddresBuffer
  };

  struct BindPoint {
    BufferViewType viewType;
    D3D12_DESCRIPTOR_RANGE_TYPE descriptorType;
    uint32_t offsetInDescriptorsFromTableStart;
    uint32_t structureByteStride;
    std::string descName;
  };

  using BindingData = std::tuple<std::vector<D3D12_DESCRIPTOR_RANGE1>,
                                 std::vector<BindPoint>>;

  void CreateRootSignatureAndBindingMap(ComPtr<dxc::ID3D12ShaderReflection>& _reflection,
                                        std::vector<BindPoint>& _bindpoints,
                                        ComPtr<ID3DBlob>& rootSignatureBlob);
  BindingData ReflectBindingData(std::vector<D3D12_SHADER_INPUT_BIND_DESC> shaderInputDescs);
  BufferViewType GetViewType(const D3D12_SHADER_INPUT_BIND_DESC& desc);
  D3D12_DESCRIPTOR_RANGE_TYPE GetDescriptorRangeType(const D3D12_SHADER_INPUT_BIND_DESC& desc);

  // Some common data
 private:
  friend class DirectXDevice;
  FunctionInfo _func_info;
  std::string _entry_function;
  std::string _comment;
  std::string _hlsl_source;
  std::unordered_map<std::string, FunctionDescriptor> _func_descs;
  std::vector<BindPoint> _bindpoints;
  ComPtr<dxc::ID3D12ShaderReflection> _reflection;
  ComPtr<ID3D10Blob> _rootsig_blob;
  void binding_buffers(DirectXDevice* _dxdev, ComPtr<ID3D12DescriptorHeap> heap,
                       const std::vector<BindPoint>& _bindpoints,
                       const std::vector<std::shared_ptr<DirectBuffer>>& _buf);
  // This is to replace thread configure before launch and recompile
  ComPtr<dxc::IDxcBlob> dxc_compile_with_threads(const std::string& src, std::string entry,
                                                 std::vector<uint32_t> threads);
  // todo(wenxh): Support DXIL&DXC later;
  void dxc_compile(ComPtr<dxc::IDxcBlob>& entry_blob,
                   ComPtr<dxc::ID3D12ShaderReflection>& reflection_blob, const std::string& src,
                   std::string entry_point, std::string profile = "cs_6_0");
};

}  // namespace dx
}  // namespace runtime
}  // namespace tvm