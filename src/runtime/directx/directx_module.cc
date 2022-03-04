#include "directx_common.h"

using namespace tvm::runtime;
using namespace tvm::runtime::dx;

DirectXWrappedFunc::DirectXWrappedFunc(DirectXModuleNode* m, ObjectPtr<Object> sptr,
                                       const std::string& func_name, const FunctionInfo& info) {
  m_ = m;
  sptr_ = sptr;
  func_name_ = func_name;
  num_buffers_ = info.arg_types.size();
  launch_param_config_.Init(num_buffers_, info.launch_param_tags);
  m->compute_kernel_->set_function_info(info);
}

// invoke the function with void arguments
void DirectXWrappedFunc::operator()(TVMArgs args, TVMRetValue* rv, void** void_args) const {
  // DirectX kernels:
  //    - Same arguments in one hlsl file, even different function;
  //    - No use of return value;
  //    - The pointer to memory is actually the index for buffer in workspace;
  ThreadWorkLoad wl = launch_param_config_.Extract(args);
  auto dxdev = m_->workspace_->GetDefaultDirectXDevice();
  auto dev = m_->workspace_->GetDefaultDevice();
  std::vector<std::shared_ptr<DirectBuffer>> bufs;
  for (size_t i = 0; i < num_buffers_; i++) {
    auto ptr = (void*)args[i];
    bufs.push_back(m_->workspace_->RetrieveDataSpace(dev, ptr));
  }
  std::vector<uint32_t> block = {(uint32_t)wl.grid_dim(0), (uint32_t)wl.grid_dim(1),
                                 (uint32_t)wl.grid_dim(2)};
  std::vector<uint32_t> thread = {(uint32_t)wl.block_dim(0), (uint32_t)wl.block_dim(1),
                                  (uint32_t)wl.block_dim(2)};
  // Launch the kernel with sync
  dxdev->dispatch(m_->compute_kernel_, bufs, func_name_, block, thread, false);
}

DirectXModuleNode::DirectXModuleNode(std::string data, std::string fmt,
                                     std::unordered_map<std::string, FunctionInfo> fmap,
                                     std::string source)
    : data_(data), fmt_(fmt), fmap_(fmap), source_(source) {
  workspace_ = DirectXWorkspace::Global();
  if (fmt == "hlsl") {
    data = "";
    // Compile hlsl code;
    compute_kernel_ = std::make_shared<DirectComputeKernel>(source_);
  } else if (fmt == "dxbc")
    throw std::invalid_argument(_msg_("DirectX Module will support DXBC later."));
  else if (fmt == "dxil")
    throw std::invalid_argument(_msg_("DirectX Module will support DXIL later."));
  else
    throw std::invalid_argument(_msg_("DirectX Module created with invalid format."));
  type_key_ = std::string("directx") + std::string(":") + fmt;
}

void DirectXModuleNode::SaveToFile(const std::string& file_name, const std::string& format) {
  std::string fmt = GetFileFormat(file_name, format);
  std::string meta_file = GetMetaFilePath(file_name);
  if (fmt == "hlsl") {
    ICHECK_NE(source_.length(), 0);
    SaveMetaDataToFile(meta_file, fmap_);
    SaveBinaryToFile(file_name, source_);
  } else {
    throw std::invalid_argument(_msg_("DirectX Module created with invalid format."));
  }
}

void DirectXModuleNode::SaveToBinary(dmlc::Stream* stream) {
  stream->Write(fmt_);
  stream->Write(fmap_);
  stream->Write(data_);
}

std::string DirectXModuleNode::GetSource(const std::string& format) {
  if (format.empty() || format == "hlsl")
    return source_;
  else if (format == fmt_)
    return data_;
  throw std::invalid_argument(_msg_("DirectX Module: invalid format error."));
}

PackedFunc DirectXModuleNode::GetFunction(const std::string& name,
                                          const ObjectPtr<Object>& sptr_to_self) {
  ICHECK_EQ(sptr_to_self.get(), this);
  auto it = fmap_.find(name);
  if (it == fmap_.end()) return PackedFunc();
  FunctionInfo& info = it->second;
  DirectXWrappedFunc f(this, sptr_to_self, name, info);
  return PackFuncVoidAddr(f, info.arg_types);
}

Module tvm::runtime::DirectXModuleCreate(std::string data, std::string fmt,
                                         std::unordered_map<std::string, FunctionInfo> fmap,
                                         std::string source) {
  auto n = make_object<DirectXModuleNode>(data, fmt, fmap, source);
  return Module(n);
}

// Load module from module.
Module tvm::runtime::DirectXModuleLoadFile(const std::string& file_name,
                                           const std::string& format) {
  std::string data;
  std::unordered_map<std::string, FunctionInfo> fmap;
  std::string fmt = GetFileFormat(file_name, format);
  std::string meta_file = GetMetaFilePath(file_name);
  if (fmt == "hlsl") {
    LoadBinaryFromFile(file_name, &data);
    LoadMetaDataFromFile(meta_file, &fmap);
    return DirectXModuleCreate("", fmt, fmap, data);
  } else {
    throw std::invalid_argument(_msg_("DirectX Module loaded with invalid format."));
  }
}

TVM_REGISTER_GLOBAL("runtime.module.loadfile_hlsl").set_body_typed(DirectXModuleLoadFile);

TVM_REGISTER_GLOBAL("device_api.directx").set_body([](TVMArgs args, TVMRetValue* rv) {
  DeviceAPI* ptr = DirectXWorkspace::Global();
  *rv = static_cast<void*>(ptr);
});

TVM_REGISTER_GLOBAL("device_api.directx_host").set_body([](TVMArgs args, TVMRetValue* rv) {
  DeviceAPI* ptr = DirectXWorkspace::Global();
  *rv = static_cast<void*>(ptr);
});

TVM_REGISTER_GLOBAL("device_api.directx_upload").set_body([](TVMArgs args, TVMRetValue* rv) {
  DeviceAPI* ptr = DirectXWorkspace::Global();
  *rv = static_cast<void*>(ptr);
});

TVM_REGISTER_GLOBAL("device_api.directx_readback").set_body([](TVMArgs args, TVMRetValue* rv) {
  DeviceAPI* ptr = DirectXWorkspace::Global();
  *rv = static_cast<void*>(ptr);
});
