# HIXL Python API - pybind11 Implementation Design

## Goal

Reimplement the HIXL Python API (from commit 678821c, `support_python_api_ctypes` branch) using pybind11 instead of ctypes, keeping the same Python interface parameters. No Python wrapper layer - all logic in C++ `HixlPy` class exposed directly via pybind11 module.

## Architecture

Single-layer: C++ wrapper class → pybind11 module → Python user imports directly.

```
src/python/hixl_wrapper/
  hixl_py.h          # HixlPy wrapper class (handle tracking + type conversion + GIL release)
  hixl_py.cpp        # PYBIND11_MODULE(hixl, m) bindings
  CMakeLists.txt      # pybind11_add_module(hixl) → hixl.so
```

No `src/python/hixl/hixl/` Python package. Python users do `import hixl; api = hixl.Hixl()`.

## C++ HixlPy Wrapper Class

Holds `hixl::Hixl*` instance + handle tracking maps. All methods have Python-friendly signatures matching 678821c's Python API:

### Handle Tracking
- `std::map<int64_t, hixl::MemHandle> memHandles_` with counter-based IDs
- `std::map<int64_t, hixl::TransferReq> reqHandles_` with counter-based IDs

### Method Signatures (matching 678821c Python API)
| Method | C++ Signature | Notes |
|--------|---------------|-------|
| initialize | `void initialize(std::string localEngine, std::optional<std::map<std::string,std::string>> options)` | string→AscendString conversion |
| finalize | `void finalize()` | deregisters all tracked mem handles first |
| register_mem | `int64_t registerMem(hixl::MemDesc desc, hixl::MemType type)` | returns handle ID, stores MemHandle |
| deregister_mem | `void deregisterMem(int64_t handleId)` | looks up MemHandle, removes from map |
| connect | `void connect(std::string remoteEngine, int32_t timeout=1000)` | string→AscendString |
| disconnect | `void disconnect(std::string remoteEngine, int32_t timeout=1000)` | string→AscendString |
| transfer_sync | `void transferSync(std::string remoteEngine, hixl::TransferOp op, std::vector<hixl::TransferOpDesc> opDescs, int32_t timeout=1000)` | |
| transfer_async | `int64_t transferAsync(std::string remoteEngine, hixl::TransferOp op, std::vector<hixl::TransferOpDesc> opDescs, std::optional<hixl::TransferArgs> args)` | returns req ID |
| get_transfer_status | `hixl::TransferStatus getTransferStatus(int64_t reqId, bool autoCleanup=true)` | looks up TransferReq |
| send_notify | `void sendNotify(std::string remoteEngine, std::string name, std::string msg, int32_t timeout=1000)` | string→NotifyDesc |
| get_notifies | `std::vector<std::pair<std::string,std::string>> getNotifies()` | NotifyDesc→(name,msg) pairs |

### GIL Management
- Blocking operations (initialize, connect, transfer_sync, etc.) use `pybind11::gil_scoped_release`
- Follows existing `llm_wrapper_v2.cc` pattern in repo

## pybind11 Module Bindings

`PYBIND11_MODULE(hixl, m)` exposes:
- `py::class_<HixlPy> hixl` named "Hixl" with all methods
- `py::class_<hixl::MemDesc>` with `.def(py::init<>())`, `.def_readwrite("addr")`, `.def_readwrite("len")`
- `py::class_<hixl::TransferOpDesc>` with `.def(py::init<>())`, `.def_readwrite("local_addr")`, `.def_readwrite("remote_addr")`, `.def_readwrite("len")`
- `py::class_<hixl::TransferArgs>` with `.def(py::init<>())`
- `py::enum_<hixl::MemType>` (MEM_DEVICE, MEM_HOST)
- `py::enum_<hixl::TransferOp>` (READ, WRITE)
- `py::enum_<hixl::TransferStatus>` (WAITING, COMPLETED, TIMEOUT, FAILED)
- Custom `HixlException` registered with `py::register_exception_translator`

## Build Integration

- `src/python/hixl_wrapper/CMakeLists.txt`: `pybind11_add_module(hixl hixl_py.cpp)` linking to `cann_hixl`, matching `llm_wrapper/CMakeLists.txt` pattern
- `src/python/CMakeLists.txt`: add `add_subdirectory(hixl_wrapper)`
- Output: `hixl.so` pybind11 module

## Examples

Two example scripts matching 678821c, adapted for pybind11 imports:
- `examples/python/client_server_h2d.py`: uses `import hixl; hixl.Hixl()`, `hixl.MemDesc`, `hixl.MemType`, etc.
- `benchmarks/python/benchmark_transfer_bandwidth.py`: D2D/H2D/D2H/H2H benchmark with param conversion timing removed (no ctypes overhead), chunk-based transfers, throughput calculation

## Reference Files
- Mooncake: `/home/l00653936/workspace/Mooncake/mooncake-integration/transfer_engine/transfer_engine_py.h`, `transfer_engine_py.cpp`
- HIXL existing: `src/python/llm_wrapper/llm_wrapper_v2.cc`, `src/python/llm_wrapper/CMakeLists.txt`
- HIXL headers: `include/hixl/hixl.h`, `include/hixl/hixl_types.h`
- 678821c examples: `examples/python/client_server_h2d.py`, `benchmarks/python/benchmark_transfer_bandwidth.py`
