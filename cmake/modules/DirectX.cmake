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
  # Looking for dx libs
  list(APPEND dx_dep_libs "")
 
  # check if in wsl
  if(EXISTS "/dev/dxg")
    find_library(libd3d12 d3d12 HINTS /lib/wsl/lib)
    find_library(libd3d12core d3d12core HINTS /lib/wsl/lib)
    find_library(libdxcore dxcore HINTS /lib/wsl/lib)
    list(APPEND dx_dep_libs ${libd3d12} ${libd3d12core} ${libdxcore})
  endif()

  message(STATUS "Build with DirectX support")
  find_package(directx-headers CONFIG REQUIRED)
  file(GLOB RUNTIME_DIRECTX_SRCS src/runtime/directx/*.cc)
  add_library(dx_lib ${RUNTIME_DIRECTX_SRCS})
  target_link_libraries(dx_lib PRIVATE Microsoft::DirectX-Headers ${dx_dep_libs})
  list(APPEND TVM_RUNTIME_LINKER_LIBS dx_lib)
  if(GTEST_FOUND)
    file(GLOB RUNTIME_TEST_DIRECTX_SRCS src/runtime/directx/test/*.cc)
	add_executable(dx_test ${RUNTIME_TEST_DIRECTX_SRCS})
    target_link_libraries(dx_test tvm_libinfo_objs tvm_objs tvm_runtime_objs GTest::GTest GTest::Main dx_lib)
	gtest_discover_tests(dx_test)
  endif()
else()
  list(APPEND COMPILER_SRCS src/target/opt/build_directx_off.cc)
endif(USE_DIRECTX)
