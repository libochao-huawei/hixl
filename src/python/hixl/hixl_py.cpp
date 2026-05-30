/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_py.h"

#include <stdexcept>

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

namespace hixl {

HixlPy::HixlPy() : hixl_(std::make_unique<Hixl>()) {}

HixlPy::~HixlPy() = default;

void HixlPy::checkStatus(Status status, const std::string &context) {
  if (status != SUCCESS) {
    throw std::runtime_error("[Status " + std::to_string(status) + "] " + context);
  }
}

void HixlPy::initialize(const std::string &local_engine,
                        const std::optional<std::map<std::string, std::string>> &options) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_) {
      throw std::runtime_error("Already initialized");
    }
  }

  std::map<AscendString, AscendString> ascend_options;
  if (options.has_value()) {
    for (const auto &kv : options.value()) {
      ascend_options.emplace(AscendString(kv.first.c_str()), AscendString(kv.second.c_str()));
    }
  }

  pybind11::gil_scoped_release release;
  Status status = hixl_->Initialize(AscendString(local_engine.c_str()), ascend_options);

  pybind11::gil_scoped_acquire acquire;
  if (status == SUCCESS) {
    std::lock_guard<std::mutex> guard(mutex_);
    initialized_ = true;
  }
  checkStatus(status, "Initialize failed");
}

void HixlPy::finalize() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_) {
    return;
  }
  initialized_ = false;
}

int64_t HixlPy::registerMem(const MemDesc &mem, MemType type) {
  throw std::runtime_error("register_mem is not supported");
}

void HixlPy::deregisterMem(int64_t handle_id) {
  throw std::runtime_error("deregister_mem is not supported");
}

void HixlPy::connect(const std::string &remote_engine, int32_t timeout) {
  throw std::runtime_error("connect is not supported");
}

void HixlPy::disconnect(const std::string &remote_engine, int32_t timeout) {
  throw std::runtime_error("disconnect is not supported");
}

void HixlPy::transferSync(const std::string &remote_engine, TransferOp op, const std::vector<TransferOpDesc> &op_descs,
                          int32_t timeout) {
  throw std::runtime_error("transfer_sync is not supported");
}

int64_t HixlPy::transferAsync(const std::string &remote_engine, TransferOp op,
                              const std::vector<TransferOpDesc> &op_descs,
                              const std::optional<TransferArgs> &optional_args) {
  throw std::runtime_error("transfer_async is not supported");
}

TransferStatus HixlPy::getTransferStatus(int64_t req_id, bool auto_cleanup) {
  throw std::runtime_error("get_transfer_status is not supported");
}

void HixlPy::sendNotify(const std::string &remote_engine, const std::string &name, const std::string &msg,
                        int32_t timeout) {
  throw std::runtime_error("send_notify is not supported");
}

std::vector<std::pair<std::string, std::string>> HixlPy::getNotifies() {
  throw std::runtime_error("get_notifies is not supported");
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

  py::class_<hixl::MemDesc>(m, "MemDesc")
      .def(py::init<>())
      .def(py::init<uintptr_t, size_t>(), py::arg("addr"), py::arg("len"))
      .def_readwrite("addr", &hixl::MemDesc::addr)
      .def_readwrite("len", &hixl::MemDesc::len)
      .def("__repr__", [](const hixl::MemDesc &m) {
        return "MemDesc(addr=" + std::to_string(m.addr) + ", len=" + std::to_string(m.len) + ")";
      });

  py::class_<hixl::TransferOpDesc>(m, "TransferOpDesc")
      .def(py::init<>())
      .def(py::init<uintptr_t, uintptr_t, size_t>(), py::arg("local_addr"), py::arg("remote_addr"), py::arg("len"))
      .def_readwrite("local_addr", &hixl::TransferOpDesc::local_addr)
      .def_readwrite("remote_addr", &hixl::TransferOpDesc::remote_addr)
      .def_readwrite("len", &hixl::TransferOpDesc::len)
      .def("__repr__", [](const hixl::TransferOpDesc &d) {
        return "TransferOpDesc(local_addr=" + std::to_string(d.local_addr) +
               ", remote_addr=" + std::to_string(d.remote_addr) + ", len=" + std::to_string(d.len) + ")";
      });

  py::class_<hixl::TransferArgs>(m, "TransferArgs").def(py::init<>());

  py::class_<hixl::HixlPy>(m, "Hixl")
      .def(py::init<>())
      .def("__del__",
           [](hixl::HixlPy &self) {
             try {
               self.finalize();
             } catch (...) {
             }
           })
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
      .def("get_transfer_status", &hixl::HixlPy::getTransferStatus, py::arg("req_handle_id"),
           py::arg("auto_cleanup") = true)
      .def("send_notify", &hixl::HixlPy::sendNotify, py::arg("remote_engine"), py::arg("notify_name"),
           py::arg("notify_msg"), py::arg("timeout") = 1000)
      .def("get_notifies", &hixl::HixlPy::getNotifies);

  static py::exception<std::runtime_error> hixl_exc(m, "HixlException");
  py::register_exception_translator([](std::exception_ptr p) {
    try {
      if (p) std::rethrow_exception(p);
    } catch (std::runtime_error &e) {
      PyErr_SetString(hixl_exc.ptr(), e.what());
    }
  });
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
