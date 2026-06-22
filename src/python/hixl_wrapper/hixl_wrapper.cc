/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "Python.h"
#ifdef ASCEND_CI_LIMITED_PY37
#undef PyCFunction_NewEx
#endif

#include "pybind11/pybind11.h"
#include "hixl/hixl_types.h"
#include "hixl_engine_wrapper.h"

#undef PYBIND11_CHECK_PYTHON_VERSION
#define PYBIND11_CHECK_PYTHON_VERSION

namespace hixl_wrapper {
namespace {
namespace py = pybind11;
void BindStatusCodes(py::module &m) {
  //把 C++ 的常量和枚举值"翻译"成 Python 模块的属性，让 Python 用户能用 hixl_engine_wrapper.kSuccess 这样的名字来引用 C++ 的数字常量。
  m.attr("kSuccess") = py::int_(hixl::SUCCESS);
  m.attr("kFailed") = py::int_(hixl::FAILED);
  m.attr("kParamInvalid") = py::int_(hixl::PARAM_INVALID);
  m.attr("kTimeout") = py::int_(hixl::TIMEOUT);
  m.attr("kNotConnected") = py::int_(hixl::NOT_CONNECTED);
  m.attr("kAlreadyConnected") = py::int_(hixl::ALREADY_CONNECTED);
  m.attr("kNotifyFailed") = py::int_(hixl::NOTIFY_FAILED);
  m.attr("kUnsupported") = py::int_(hixl::UNSUPPORTED);
  m.attr("kResourceExhausted") = py::int_(hixl::RESOURCE_EXHAUSTED);
  m.attr("kMemDevice") = py::int_(hixl::MEM_DEVICE);
  m.attr("kMemHost") = py::int_(hixl::MEM_HOST);
  m.attr("kRead") = py::int_(hixl::READ);
  m.attr("kWrite") = py::int_(hixl::WRITE);
  m.attr("kTransferStatusWaiting") = py::int_(static_cast<uint32_t>(hixl::TransferStatus::WAITING));
  m.attr("kTransferStatusCompleted") = py::int_(static_cast<uint32_t>(hixl::TransferStatus::COMPLETED));
  m.attr("kTransferStatusTimeout") = py::int_(static_cast<uint32_t>(hixl::TransferStatus::TIMEOUT));
  m.attr("kTransferStatusFailed") = py::int_(static_cast<uint32_t>(hixl::TransferStatus::FAILED));
  m.attr("kAsyncConnectStatusNotConnect") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::NOT_CONNECT));
  m.attr("kAsyncConnectStatusConnectPending") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::CONNECT_PENDING));
  m.attr("kAsyncConnectStatusConnecting") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::CONNECTING));
  m.attr("kAsyncConnectStatusConnected") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::CONNECTED));
  m.attr("kAsyncConnectStatusConnectFailed") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::CONNECT_FAILED));
  m.attr("kAsyncConnectStatusDisconnectPending") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::DISCONNECT_PENDING));
  m.attr("kAsyncConnectStatusDisconnecting") = py::int_(static_cast<uint32_t>(hixl::AsyncConnectStatus::DISCONNECTING));
  m.attr("kOptionEnableUseFabricMem") = py::str(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  m.attr("kOptionRdmaTrafficClass") = py::str(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  m.attr("kOptionRdmaServiceLevel") = py::str(hixl::OPTION_RDMA_SERVICE_LEVEL);
  m.attr("kOptionBufferPool") = py::str(hixl::OPTION_BUFFER_POOL);
  m.attr("kOptionGlobalResourceConfig") = py::str(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  m.attr("kOptionAutoConnect") = py::str(hixl::OPTION_AUTO_CONNECT);
  m.attr("kOptionLocalCommRes") = py::str(hixl::OPTION_LOCAL_COMM_RES);
  m.attr("kFeatureAutoConnect") = py::int_(hixl::FeatureType::AUTO_CONNECT);
  m.attr("kFeatureClientServerComm") = py::int_(hixl::FeatureType::CLIENT_SERVER_COMM);
  m.attr("kFeatureSupported") = py::int_(hixl::FEATURE_SUPPORTED);
  m.attr("kFeatureNotSupported") = py::int_(hixl::FEATURE_NOT_SUPPORTED);
}

void BuildHixlFuncs(py::module &m) {
  // 所有方法使用 py::call_guard<py::gil_scoped_release>()：进入 C++ 函数前释放 GIL，C++ 函数返回后重新获取 GIL
  // C++ 操作不访问 Python 对象，释放 GIL 让其他 Python 线程并发执行
  // 用户调用 hixl_engine_wrapper.initialize(...)，实际执行这个 &HixlEngineWrapper::Initialize C++ 函数
  (void)m.def("initialize", &HixlEngineWrapper::Initialize, py::call_guard<py::gil_scoped_release>());
  (void)m.def("finalize", &HixlEngineWrapper::Finalize, py::call_guard<py::gil_scoped_release>());
  (void)m.def("register_mem", &HixlEngineWrapper::RegisterMem, py::call_guard<py::gil_scoped_release>());
  (void)m.def("deregister_mem", &HixlEngineWrapper::DeregisterMem, py::call_guard<py::gil_scoped_release>());
  (void)m.def("connect", &HixlEngineWrapper::Connect, py::call_guard<py::gil_scoped_release>());
  (void)m.def("disconnect", &HixlEngineWrapper::Disconnect, py::call_guard<py::gil_scoped_release>());
  (void)m.def("connect_async", &HixlEngineWrapper::ConnectAsync, py::call_guard<py::gil_scoped_release>());
  (void)m.def("disconnect_async", &HixlEngineWrapper::DisconnectAsync, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_async_connect_status", &HixlEngineWrapper::GetAsyncConnectStatus, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_all_async_connect_status", &HixlEngineWrapper::GetAllAsyncConnectStatus, py::call_guard<py::gil_scoped_release>());
  (void)m.def("transfer_sync", &HixlEngineWrapper::TransferSync, py::call_guard<py::gil_scoped_release>());
  (void)m.def("transfer_async", &HixlEngineWrapper::TransferAsync, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_transfer_status", &HixlEngineWrapper::GetTransferStatus, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_transfer_status_batch", &HixlEngineWrapper::GetTransferStatusBatch, py::call_guard<py::gil_scoped_release>());
  (void)m.def("send_notify", &HixlEngineWrapper::SendNotify, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_notifies", &HixlEngineWrapper::GetNotifies, py::call_guard<py::gil_scoped_release>());
  (void)m.def("get_capability", &HixlEngineWrapper::GetCapability, py::call_guard<py::gil_scoped_release>());
}
}  // namespace

PYBIND11_MODULE(hixl_engine_wrapper, m) {
  BindStatusCodes(m);
  BuildHixlFuncs(m);
}
}  // namespace hixl_wrapper
