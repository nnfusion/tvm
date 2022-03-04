#pragma once
#include <dmlc/logging.h>
#include <gtest/gtest.h>

#include "../directx_common.h"

using namespace tvm::runtime;
using namespace tvm::runtime::dx;

#ifdef INIT
extern const std::string hlsl_str_inc = R"(
RWStructuredBuffer<int> buf : register(u0);

[numthreads(1, 1, 1)] void CSMain() {
  for (int i = 0; i < 4; i++) buf[i] = buf[i] + 1;
}

)";

extern const std::string hlsl_str_add = R"(
RWStructuredBuffer<int> b0 : register(u0);
RWStructuredBuffer<int> b1 : register(u1);
RWStructuredBuffer<int> b2 : register(u2);

[numthreads(1, 1, 1)] void CSMain(uint3 DTid
                                  : SV_DispatchThreadID, 
                                  uint blockIdx : SV_GroupID) {
  for (int i = 0; i < 4; i++) b2[i] = b0[i] + b1[i];
}
)";

extern const std::string hlsl_str_vector_add = R"(
RWStructuredBuffer<float> C: register(u0);
RWStructuredBuffer<float> A: register(u1);
RWStructuredBuffer<float> B: register(u2);

// only use uint, not uint3 to access the launch config!
[numthreads(1, 1, 1)] void CSMain(uint blockIdx : SV_GroupID,  uint threadIdx : SV_GroupThreadID) {
  if (((int)threadIdx) < 4) {
    C[(((int)threadIdx))] = (A[(((int)threadIdx))] + B[(((int)threadIdx))]);
  }
}
)";

#else
extern const std::string hlsl_str_add;
extern const std::string hlsl_str_inc;
extern const std::string hlsl_str_vector_add;
#endif

FunctionInfo create_funcinfo(int argsnum);

template <typename T>
void set_tensor(std::shared_ptr<DirectHostBuffer> bf, std::vector<T> vals) {
  int* ptr = (int*)bf->open_data_ptr();
  for (int i = 0; i < vals.size(); i++) ptr[i] = vals[i];
  bf->close_data_ptr();
}

template <typename T>
void check_tensor(std::shared_ptr<DirectHostBuffer> bf, std::vector<T> vals) {
  int* ptr = (int*)bf->open_data_ptr();
  for (int i = 0; i < vals.size(); i++) ASSERT_EQ(ptr[i], vals[i]);
  bf->close_data_ptr();
}

template <typename T>
void set_tensor(DirectHostBuffer* bf, std::vector<T> vals) {
  int* ptr = (int*)bf->open_data_ptr();
  for (int i = 0; i < vals.size(); i++) ptr[i] = vals[i];
  bf->close_data_ptr();
}

template <typename T>
void check_tensor(DirectHostBuffer* bf, std::vector<T> vals) {
  int* ptr = (int*)bf->open_data_ptr();
  for (int i = 0; i < vals.size(); i++) ASSERT_EQ(ptr[i], vals[i]);
  bf->close_data_ptr();
}