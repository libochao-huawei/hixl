/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_py.h"

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

namespace hixl {

HixlPy::HixlPy() : hixl_(std::make_unique<Hixl>()) {}

HixlPy::~HixlPy() = default;

Status HixlPy::initialize(const std::string &local_engine,
                          const std::optional<std::map<std::string, std::string>> &options) {
  std::map<AscendString, AscendString> ascend_options;
  if (options.has_value()) {
    for (const auto &kv : options.value()) {
      ascend_options.emplace(AscendString(kv.first.c_str()), AscendString(kv.second.c_str()));
    }
  }
  pybind11::gil_scoped_release release;
  return hixl_->Initialize(AscendString(local_engine.c_str()), ascend_options);
}

void HixlPy::finalize() {
  pybind11::gil_scoped_release release;
  hixl_->Finalize();
}

std::tuple<Status, int64_t> HixlPy::registerMem(const MemDesc &mem, MemType type) {
  (void)mem;
  (void)type;
  return {UNSUPPORTED, 0};
}

Status HixlPy::deregisterMem(int64_t handle_id) {
  (void)handle_id;
  return UNSUPPORTED;
}

Status HixlPy::connect(const std::string &remote_engine, int32_t timeout) {
  (void)remote_engine;
  (void)timeout;
  return UNSUPPORTED;
}

Status HixlPy::disconnect(const std::string &remote_engine, int32_t timeout) {
  (void)remote_engine;
  (void)timeout;
  return UNSUPPORTED;
}

Status HixlPy::transferSync(const std::string &remote_engine, TransferOp op,
                            const std::vector<TransferOpDesc> &op_descs, int32_t timeout) {
  (void)remote_engine;
  (void)op;
  (void)op_descs;
  (void)timeout;
  return UNSUPPORTED;
}

std::tuple<Status, int64_t> HixlPy::transferAsync(const std::string &remote_engine, TransferOp op,
                                                  const std::vector<TransferOpDesc> &op_descs,
                                                  const std::optional<TransferArgs> &optional_args) {
  (void)remote_engine;
  (void)op;
  (void)op_descs;
  (void)optional_args;
  return {UNSUPPORTED, 0};
}

std::tuple<Status, TransferStatus> HixlPy::getTransferStatus(int64_t req_id) {
  (void)req_id;
  return {UNSUPPORTED, TransferStatus::WAITING};
}

Status HixlPy::sendNotify(const std::string &remote_engine, const std::string &name, const std::string &msg,
                          int32_t timeout) {
  (void)remote_engine;
  (void)name;
  (void)msg;
  (void)timeout;
  return UNSUPPORTED;
}

std::tuple<Status, std::vector<std::pair<std::string, std::string>>> HixlPy::getNotifies() {
  return {UNSUPPORTED, {}};
}

Status HixlPy::connectAsync(const std::string &remote_engine, int32_t timeout) {
  (void)remote_engine;
  (void)timeout;
  return UNSUPPORTED;
}

Status HixlPy::disconnectAsync(const std::string &remote_engine, int32_t timeout) {
  (void)remote_engine;
  (void)timeout;
  return UNSUPPORTED;
}

std::tuple<Status, AsyncConnectStatus> HixlPy::getAsyncConnectStatus(const std::string &remote_engine) {
  (void)remote_engine;
  return {UNSUPPORTED, AsyncConnectStatus::NOT_CONNECT};
}

std::tuple<Status, std::vector<std::pair<std::string, AsyncConnectStatus>>> HixlPy::getAsyncConnectStatusAll() {
  return {UNSUPPORTED, {}};
}

}  // namespace hixl

namespace py = pybind11;

PYBIND11_MODULE(hixl, m) {
  m.doc() = "HIXL Python API";

  py::enum_<hixl::MemType>(m, "MemType", py::arithmetic())
      .value("DEVICE", hixl::MemType::MEM_DEVICE)
      .value("HOST", hixl::MemType::MEM_HOST)
      .export_values();

  py::enum_<hixl::TransferOp>(m, "TransferOp", py::arithmetic())
      .value("READ", hixl::TransferOp::READ)
      .value("WRITE", hixl::TransferOp::WRITE)
      .export_values();

  py::enum_<hixl::TransferStatus>(m, "TransferStatus")
      .value("WAITING", hixl::TransferStatus::WAITING)
      .value("COMPLETED", hixl::TransferStatus::COMPLETED)
      .value("TIMEOUT", hixl::TransferStatus::TIMEOUT)
      .value("FAILED", hixl::TransferStatus::FAILED)
      .export_values();

  py::enum_<hixl::AsyncConnectStatus>(m, "AsyncConnectStatus")
      .value("NOT_CONNECT", hixl::AsyncConnectStatus::NOT_CONNECT)
      .value("CONNECT_PENDING", hixl::AsyncConnectStatus::CONNECT_PENDING)
      .value("CONNECTING", hixl::AsyncConnectStatus::CONNECTING)
      .value("CONNECTED", hixl::AsyncConnectStatus::CONNECTED)
      .value("CONNECT_FAILED", hixl::AsyncConnectStatus::CONNECT_FAILED)
      .value("DISCONNECT_PENDING", hixl::AsyncConnectStatus::DISCONNECT_PENDING)
      .value("DISCONNECTING", hixl::AsyncConnectStatus::DISCONNECTING)
      .export_values();

  py::class_<hixl::MemDesc>(m, "MemDesc")
      .def(py::init([]() { return hixl::MemDesc{0, 0}; }))
      .def(py::init<uintptr_t, size_t>(), py::arg("addr"), py::arg("len"))
      .def_readwrite("addr", &hixl::MemDesc::addr)
      .def_readwrite("len", &hixl::MemDesc::len)
      .def("__repr__", [](const hixl::MemDesc &m) {
        return "MemDesc(addr=" + std::to_string(m.addr) + ", len=" + std::to_string(m.len) + ")";
      });

  py::class_<hixl::TransferOpDesc>(m, "TransferOpDesc")
      .def(py::init([]() { return hixl::TransferOpDesc{0, 0, 0}; }))
      .def(py::init<uintptr_t, uintptr_t, size_t>(), py::arg("local_addr"), py::arg("remote_addr"), py::arg("len"))
      .def_readwrite("local_addr", &hixl::TransferOpDesc::local_addr)
      .def_readwrite("remote_addr", &hixl::TransferOpDesc::remote_addr)
      .def_readwrite("len", &hixl::TransferOpDesc::len)
      .def("__repr__", [](const hixl::TransferOpDesc &d) {
        return "TransferOpDesc(local_addr=" + std::to_string(d.local_addr) +
               ", remote_addr=" + std::to_string(d.remote_addr) + ", len=" + std::to_string(d.len) + ")";
      });

  py::class_<hixl::TransferArgs>(m, "TransferArgs").def(py::init<>());

  py::class_<hixl::NotifyDesc>(m, "NotifyDesc")
      .def(py::init<>())
      .def_property(
          "name", [](const hixl::NotifyDesc &n) { return std::string(n.name.GetString()); },
          [](hixl::NotifyDesc &n, const std::string &s) { n.name = hixl::AscendString(s.c_str()); })
      .def_property(
          "notify_msg", [](const hixl::NotifyDesc &n) { return std::string(n.notify_msg.GetString()); },
          [](hixl::NotifyDesc &n, const std::string &s) { n.notify_msg = hixl::AscendString(s.c_str()); })
      .def("__repr__", [](const hixl::NotifyDesc &n) {
        return "NotifyDesc(name=" + std::string(n.name.GetString()) +
               ", notify_msg=" + std::string(n.notify_msg.GetString()) + ")";
      });

  py::class_<hixl::HixlPy>(m, "Hixl")
      .def(py::init<>())
      .def("initialize", &hixl::HixlPy::initialize, py::arg("local_engine"),
           py::arg("options") = std::optional<std::map<std::string, std::string>>())
      .def("finalize", &hixl::HixlPy::finalize)
      .def("register_mem", &hixl::HixlPy::registerMem, py::arg("mem_desc"), py::arg("mem_type"))
      .def("deregister_mem", &hixl::HixlPy::deregisterMem, py::arg("handle_id"))
      .def("connect", &hixl::HixlPy::connect, py::arg("remote_engine"), py::arg("timeout") = 1000)
      .def("disconnect", &hixl::HixlPy::disconnect, py::arg("remote_engine"), py::arg("timeout") = 1000)
      .def("transfer_sync", &hixl::HixlPy::transferSync, py::arg("remote_engine"), py::arg("operation"),
           py::arg("op_descs"), py::arg("timeout") = 1000)
      .def("transfer_async", &hixl::HixlPy::transferAsync, py::arg("remote_engine"), py::arg("operation"),
           py::arg("op_descs"), py::arg("optional_args") = std::optional<hixl::TransferArgs>())
      .def("get_transfer_status", &hixl::HixlPy::getTransferStatus, py::arg("req_handle_id"))
      .def("send_notify", &hixl::HixlPy::sendNotify, py::arg("remote_engine"), py::arg("notify_name"),
           py::arg("notify_msg"), py::arg("timeout") = 1000)
      .def("get_notifies", &hixl::HixlPy::getNotifies)
      .def("connect_async", &hixl::HixlPy::connectAsync, py::arg("remote_engine"), py::arg("timeout") = 1000)
      .def("disconnect_async", &hixl::HixlPy::disconnectAsync, py::arg("remote_engine"), py::arg("timeout") = 1000)
      .def("get_async_connect_status", &hixl::HixlPy::getAsyncConnectStatus, py::arg("remote_engine"))
      .def("get_async_connect_status_all", &hixl::HixlPy::getAsyncConnectStatusAll);

  m.attr("SUCCESS") = py::int_(hixl::SUCCESS);
  m.attr("PARAM_INVALID") = py::int_(hixl::PARAM_INVALID);
  m.attr("TIMEOUT") = py::int_(hixl::TIMEOUT);
  m.attr("NOT_CONNECTED") = py::int_(hixl::NOT_CONNECTED);
  m.attr("ALREADY_CONNECTED") = py::int_(hixl::ALREADY_CONNECTED);
  m.attr("NOTIFY_FAILED") = py::int_(hixl::NOTIFY_FAILED);
  m.attr("UNSUPPORTED") = py::int_(hixl::UNSUPPORTED);
  m.attr("FAILED") = py::int_(hixl::FAILED);
  m.attr("RESOURCE_EXHAUSTED") = py::int_(hixl::RESOURCE_EXHAUSTED);

  m.attr("OPTION_ENABLE_USE_FABRIC_MEM") = py::str(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  m.attr("OPTION_RDMA_TRAFFIC_CLASS") = py::str(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  m.attr("OPTION_RDMA_SERVICE_LEVEL") = py::str(hixl::OPTION_RDMA_SERVICE_LEVEL);
  m.attr("OPTION_BUFFER_POOL") = py::str(hixl::OPTION_BUFFER_POOL);
  m.attr("OPTION_GLOBAL_RESOURCE_CONFIG") = py::str(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  m.attr("OPTION_AUTO_CONNECT") = py::str(hixl::OPTION_AUTO_CONNECT);
  m.attr("OPTION_LOCAL_COMM_RES") = py::str(hixl::OPTION_LOCAL_COMM_RES);
}
