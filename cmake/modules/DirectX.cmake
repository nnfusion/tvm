# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# DirectX Module

if(USE_DIRECTX)
  message(STATUS "Build with DirectX support")
  find_package(directx-headers CONFIG REQUIRED)
  file(GLOB RUNTIME_DIRECTX_SRCS src/runtime/directx/*.cc)
  #list(APPEND RUNTIME_SRCS ${RUNTIME_DIRECTX_SRCS})
  #list(APPEND TVM_RUNTIME_LINKER_LIBS Microsoft::DirectX-Guids Microsoft::DirectX-Headers)
  #list(APPEND TVM_LINKER_LIBS Microsoft::DirectX-Guids Microsoft::DirectX-Headers)
  add_library(dx_runtime_lib ${RUNTIME_DIRECTX_SRCS})
  target_link_libraries(dx_runtime_lib Microsoft::DirectX-Guids Microsoft::DirectX-Headers)
  list(APPEND TVM_RUNTIME_LINKER_LIBS dx_runtime_lib)
  list(APPEND TVM_LINKER_LIBS dx_runtime_lib)
  if(GTEST_FOUND)
    file(GLOB RUNTIME_TEST_DIRECTX_SRCS src/runtime/directx/test/*.cc)
	add_executable(dx_test ${RUNTIME_TEST_DIRECTX_SRCS})
    target_link_libraries(dx_test tvm_libinfo_objs tvm_objs tvm_runtime_objs GTest::GTest GTest::Main dx_runtime_lib Microsoft::DirectX-Guids Microsoft::DirectX-Headers)
	gtest_discover_tests(dx_test)
  endif()
else()
  list(APPEND COMPILER_SRCS src/target/opt/build_directx_off.cc)
endif(USE_DIRECTX)
