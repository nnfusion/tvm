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
 * \file directx_common.h
 * \brief DirectX Runtime API TVM: we also have directx_header.h for lower interface.
 *  This file is contributed by MSRA NNFusion DNN Compiler Team.
 */

#ifndef TVM_RUNTIME_DIRECTX_DIRECTX_COMMON_H_
#define TVM_RUNTIME_DIRECTX_DIRECTX_COMMON_H_

#include <dmlc/memory_io.h>
#include <tvm/runtime/c_runtime_api.h>
#include <tvm/runtime/device_api.h>
#include <tvm/runtime/logging.h>
#include <tvm/runtime/ndarray.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../file_utils.h"
#include "../pack_args.h"
#include "../source_utils.h"
#include "../thread_storage_scope.h"
#include "../workspace_pool.h"

#include "directx_header.h"
#include "directx_module.h"
#include "directx_workspace.h"

#endif  // TVM_RUNTIME_DIRECTX_DIRECTX_COMMON_H_
