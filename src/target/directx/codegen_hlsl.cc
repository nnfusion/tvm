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
 * \file codegen_hlsl.cc
 * \brief This is based on existed codegen_metal.cc
 */
#include "codegen_hlsl.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../../runtime/metal/metal_module.h"
#include "../../runtime/thread_storage_scope.h"
#include "../build_common.h"

namespace tvm {
namespace runtime {
std::unordered_map<std::string, FunctionInfo> generate_func_info_from_source(
    const std::string& src);

/*!
 * \brief create a directx module for directx devices from data.
 *
 * \param data The module data.
 * \param fmt The format of the data, can be "hlsl".
 * \param fmap The map function information map of each function.
 */
Module DirectXModuleCreate(std::string data, std::string fmt,
                           std::unordered_map<std::string, FunctionInfo> fmap, std::string source);

Module DirectXModuleLoadFile(const std::string& file_name, const std::string& format);
}  // namespace runtime
}  // namespace tvm

using namespace tvm::runtime;

namespace tvm {
namespace codegen {

void CodeGenHLSL::InitFuncState(const PrimFunc& f) {
  CodeGenC::InitFuncState(f);
  // analyze the data;
  for (Var arg : f->params) {
    if (arg.dtype().is_handle()) {
      alloc_storage_scope_[arg.get()] = "global";
    }
  }
}

void CodeGenHLSL::PrintType(const Type& type, std::ostream& os) {  // NOLINT(*)
  if (auto* ptr = type.as<PrimTypeNode>()) {
    return PrintType(ptr->dtype, os);
  } else if (auto* ptr = type.as<PointerTypeNode>()) {
    PrintType(ptr->element_type, os);
  } else if (IsVoidType(type)) {
    os << "void";
  } else {
    LOG(FATAL) << "Type " << type << " does not have a corresponding C Type";
  }
}

CodeGenHLSL::CodeGenHLSL(Target target) : target_(target) {}

/*
---------
Mapping buffer-"buf" in source code to global buffer slot, which is "u0" and it means the
first UAV(Unordered Access View) Resource. This is a bit of complex.

RWStructuredBuffer<int> buf : register(u0);
---------
A normal hlsl compute shader kernel fucntion:

[numthreads(1, 1, 1)] void inc() {
  for (int i = 0; i < 4; i++) buf[i] = buf[i] + 1;
}
--------
*/

void CodeGenHLSL::AddFunction(const PrimFunc& f) {
  // clear previous generated state.
  this->InitFuncState(f);
  // skip the first underscore, so SSA variable starts from _1
  GetUniqueName("_");
  // add to alloc buffer type.
  auto global_symbol = f->GetAttr<String>(tvm::attr::kGlobalSymbol);
  ICHECK(global_symbol.defined())
      << "CodeGenC: Expect PrimFunc to have the global_symbol attribute";
  // Buffer arguments: output as RWStructuredBuffer
  size_t num_buffer = 0;
  for (size_t i = 0; i < f->params.size(); ++i, ++num_buffer) {
    Var v = f->params[i];
    if (!v.dtype().is_handle()) break;
    std::string vid = AllocVarID(v.get());
    if (auto* ptr = v->type_annotation.as<PointerTypeNode>()) {
      if (auto* prim = ptr->element_type.as<PrimTypeNode>()) {
        RegisterHandleType(v.get(), prim->dtype);
      }
    }
    stream << "RWStructuredBuffer<";
    std::ostringstream typestream;
    PrintType(GetType(v), typestream);
    stream << typestream.str();
    stream << "> " << vid << ": register(u" << i << ");\n";
  }

  // Constant buffer: still output as RWStructuredBuffer
  // Setup normal arguments.
  size_t nargs = f->params.size() - num_buffer;
  std::string varg = GetUniqueName("arg");
  if (nargs != 0) {
    std::string arg_buf_type = static_cast<std::string>(global_symbol.value()) + "_args_t";
    stream << "RWStructuredBuffer<" << arg_buf_type << "> " << varg << ": register(u" << num_buffer
           << ");";

    // declare the struct.
    // because arg is from buffer, so we use arg[0] instead of arg.
    decl_stream << "struct " << arg_buf_type << " {\n";
    for (size_t i = num_buffer; i < f->params.size(); ++i) {
      Var v = f->params[i];
      ICHECK(!v.dtype().is_handle());
      std::string vid = AllocVarID(v.get());
      std::ostringstream vref;
      if (v.dtype().bits() == 32) {
        decl_stream << "  ";
        PrintType(v.dtype(), decl_stream);
        decl_stream << " " << vid << "[2];\n";
        vref << varg << "[0]." << vid << "[0]";
      } else if (v.dtype().bits() == 64) {
        decl_stream << "  ";
        PrintType(v.dtype(), decl_stream);
        decl_stream << " " << vid << ";\n";
        vref << varg << "[0]." << vid;
      } else {
        // todo(wenxh): find a solution.
        throw std::runtime_error("Datatype less than 32-bit is not supported.");
      }
      var_idmap_[v.get()] = vref.str();
    }
    decl_stream << "};\n\n";
  }

  stream << "\n";

  // Function header.
  this->stream << "[numthreads(1, 1, 1)] void " << static_cast<std::string>(global_symbol.value())
               << "(";

  // Setup the thread group info.
  ICHECK_EQ(GetUniqueName("threadIdx"), "threadIdx");
  ICHECK_EQ(GetUniqueName("blockIdx"), "blockIdx");
  int work_dim = 0;
  auto thread_axis = f->GetAttr<Array<tir::IterVar>>(tir::attr::kDeviceThreadAxis).value();

  for (IterVar iv : thread_axis) {
    runtime::ThreadScope scope = runtime::ThreadScope::Create(iv->thread_tag);
    work_dim = std::max(work_dim, scope.dim_index + 1);
  }
  if (work_dim != 0) {
    // use ushort by default for now
    PrintType(DataType::UInt(thread_index_bits_, work_dim), stream);
    stream << " blockIdx : SV_GroupID,";
    stream << "  ";
    PrintType(DataType::UInt(thread_index_bits_, work_dim), stream);
    stream << " threadIdx : SV_GroupThreadID";
  }

  // bind thread axis
  for (IterVar iv : thread_axis) {
    ICHECK(!var_idmap_.count(iv->var.get()));
    std::string vname = iv->thread_tag;
    if (work_dim <= 1) {
      vname = vname.substr(0, iv->thread_tag.length() - 2);
    }
    var_idmap_[iv->var.get()] =
        CastFromTo(vname, DataType::UInt(thread_index_bits_), iv->var.dtype());
  }
  // the function scope.
  stream << ") {\n";
  int func_scope = this->BeginScope();
  this->PrintStmt(f->body);
  this->EndScope(func_scope);
  this->PrintIndent();
  this->stream << "}\n\n";
}

void CodeGenHLSL::BindThreadIndex(const IterVar& iv) {
  ICHECK(!var_idmap_.count(iv->var.get()));
  var_idmap_[iv->var.get()] =
      CastFromTo(iv->thread_tag, DataType::UInt(thread_index_bits_), iv->var.dtype());
}

void CodeGenHLSL::PrintType(DataType t, std::ostream& os) {  // NOLINT(*)
  int lanes = t.lanes();
  if (t.is_handle()) {
    ICHECK_EQ(lanes, 1) << "do not yet support vector types";
    os << "void*";
    return;
  }
  if (t == DataType::Bool()) {
    os << "bool";
    return;
  }
  bool fail = false;
  if (t.is_float()) {
    // Need to care about sizes and alignment of half3/float3 because tir representation might not
    // be aware of Metal half3/float3 details and can treat them as just three elements,
    // while sizes and alignmnents of half3/float3 are one element more (half3-8 bytes/
    // float13 - 16bytes).
    // Example of problematic pattern: filling of threadgroup packed array using float3 elements
    // by threads concurrently can lead to datarace and wrong data in threadgroup shared array.
    // packed_(half3/float3) are exactly datatypes dealing with 3 elements and per-element
    // alignment
    if (lanes == 3) {
      os << "packed_";
    }
    switch (t.bits()) {
      case 16:
        os << "half";
        break;
      case 32:
        os << "float";
        break;
      default:
        fail = true;
        break;
    }
    if (!fail && lanes == 1) return;
    if (!fail && (lanes >= 2 && lanes <= 4)) {
      os << lanes;
      return;
    }
  } else if (t.is_uint() || t.is_int()) {
    if (t.is_uint()) {
      os << 'u';
    }
    if (t.bits() == 8 && t.lanes() == 4) {
      // directly 4 8 bit int in integer.
      os << "int";
      return;
    }
    switch (t.bits()) {
      case 8:
        os << "char";
        break;
      case 16:
        os << "short";
        break;
      case 32:
        os << "int";
        break;
      case 64:
        os << "long";
        break;
      case 1:
        os << "bool";
        break;
      default:
        fail = true;
        break;
    }
    if (!fail && lanes == 1) return;
    if (!fail && (lanes >= 2 && lanes <= 4)) {
      os << lanes;
      return;
    }
  }
  LOG(FATAL) << "Cannot convert type " << t << " to HLSL type";
}

void CodeGenHLSL::PrintStorageSync(const CallNode* op) {
  const std::string& sync = op->args[0].as<StringImmNode>()->value;
  if (sync == "warp") {
    this->PrintIndent();
    this->stream << "simdgroup_barrier(mem_flags::mem_threadgroup);\n";
  } else if (sync == "shared") {
    this->PrintIndent();
    this->stream << "threadgroup_barrier(mem_flags::mem_threadgroup);\n";
  } else if (sync == "global") {
    LOG(FATAL) << "global barrier not supported";
  }
}

void CodeGenHLSL::PrintVecElemLoad(const std::string& vec, DataType t, int i,
                                   std::ostream& os) {  // NOLINT(*)
  os << vec << "[" << i << "]";
}

void CodeGenHLSL::PrintVecElemStore(const std::string& vec, DataType t, int i,
                                    const std::string& value) {
  this->PrintIndent();
  stream << vec << "[" << i << "]"
         << " = " << value << ";\n";
}

void CodeGenHLSL::PrintStorageScope(const std::string& scope, std::ostream& os) {  // NOLINT(*)
  if (scope == "global") {
    os << "device ";
  } else if (scope == "shared") {
    os << "threadgroup ";
  } else {
    os << "thread ";
  }
}

void CodeGenHLSL::VisitExpr_(const BroadcastNode* op, std::ostream& os) {  // NOLINT(*)
  std::string v = PrintExpr(op->value);
  PrintType(op->dtype, os);
  os << "(";
  for (int i = 0; i < op->lanes; ++i) {
    if (i != 0) os << ", ";
    os << v;
  }
  os << ')';
}

void CodeGenHLSL::VisitExpr_(const CallNode* op, std::ostream& os) {  // NOLINT(*)
  if (op->op.same_as(builtin::reinterpret())) {
    // generate as_type<TYPE>(ARG)
    os << "(as_type<";
    this->PrintType(op->dtype, os);
    os << ">(";
    this->PrintExpr(op->args[0], os);
    os << "))";
  } else {
    CodeGenC::VisitExpr_(op, os);
  }
}

void CodeGenHLSL::VisitExpr_(const FloatImmNode* op, std::ostream& os) {  // NOLINT(*)
  std::ostringstream temp;
  if (std::isinf(op->value)) {
    if (op->value < 0) {
      temp << "-";
    }
    temp << "INFINITY";
  } else if (std::isnan(op->value)) {
    temp << "NAN";
  } else {
    temp << std::scientific << op->value;
    if (op->dtype.bits() == 32)
      temp << 'f';
    else if (op->dtype.bits() == 16)
      temp << 'h';
  }
  MarkConst(temp.str());
  os << temp.str();
}

runtime::Module BuildHLSL(IRModule mod, Target target) {
  using tvm::runtime::Registry;
  bool output_ssa = false;

  std::stringstream code;
  std::stringstream source;
  std::string fmt = "hlsl";
  for (auto kv : mod->functions) {
    ICHECK(kv.second->IsInstance<PrimFuncNode>()) << "CodeGenHLSL: Can only take PrimFunc";
    code << "// Function: " << kv.first->name_hint << std::endl;
    CodeGenHLSL cg(target);
    cg.Init(output_ssa);
    auto f = Downcast<PrimFunc>(kv.second);
    auto calling_conv = f->GetAttr<Integer>(tvm::attr::kCallingConv);
    ICHECK(calling_conv == CallingConv::kDeviceKernelLaunch)
        << "CodeGenHLSL: expect calling_conv equals CallingConv::kDeviceKernelLaunch";
    cg.AddFunction(f);
    std::string fsource = cg.Finish();
    source << fsource;
    /*
    if (const auto* f = Registry::Get("tvm_callback_metal_compile")) {
      source << fsource;
      fsource = (*f)(fsource).operator std::string();
      fmt = "metallib";
    }
    code << fsource;
    */
  }

  return DirectXModuleCreate("", fmt, ExtractFuncInfo(mod), source.str());
}

TVM_REGISTER_GLOBAL("target.build.directx").set_body_typed(BuildHLSL);
}  // namespace codegen
}  // namespace tvm
