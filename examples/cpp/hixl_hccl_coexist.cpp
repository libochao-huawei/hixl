/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_types.h"
#include "hixl/hixl.h"

using namespace hixl;

#define CHECK_ACL(x)                                                                     \
  do {                                                                                   \
    aclError __status = x;                                                               \
    if (__status != ACL_ERROR_NONE) {                                                    \
      fprintf(stderr, "%s:%d aclError:%d\n", __FILE__, __LINE__, __status);              \
      return -1;                                                                         \
    }                                                                                    \
  } while (0)

#define HCCLCHECK(x)                                                                     \
  do {                                                                                   \
    HcclResult __status = x;                                                             \
    if (__status != HCCL_SUCCESS) {                                                      \
      fprintf(stderr, "%s:%d hcclError:%d\n", __FILE__, __LINE__, __status);             \
      return -1;                                                                         \
    }                                                                                    \
  } while (0)

#define HIXLCHECK(x)                                                                     \
  do {                                                                                   \
    Status __status = x;                                                                 \
    if (__status != SUCCESS) {                                                           \
      fprintf(stderr, "%s:%d hixlError:%u\n", __FILE__, __LINE__, __status);             \
      return -1;                                                                         \
    }                                                                                    \
  } while (0)

constexpr size_t kHcclDataSize = 1024;
constexpr size_t kHixlBufSize = 8 * 1024 * 1024;
constexpr uint8_t kFillValue = 0xAA;

struct DeviceContext {
    int32_t device_id;
    HcclComm hccl_comm;
    aclrtStream stream;
    void *send_buf;
    void *recv_buf;
    bool hcc_initialized;
    std::mutex mutex;
};

struct HixlContext {
    Hixl engine;
    int32_t device_id;
    std::string local_engine;
    std::string remote_engine;
    void *dev_buf;
    MemHandle dev_handle;
    bool initialized;
    bool connected;
};

std::vector<DeviceContext> g_device_ctxs;
HixlContext g_hixl_client;
HixlContext g_hixl_server;

std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<int> g_hcc_initialized_count{0};
std::atomic<bool> g_hixl_ready{false};
int g_dev_count = 0;

int SetupListenSocket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "socket() failed\n");
        return -1;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "bind port %d failed\n", port);
        close(fd);
        return -1;
    }
    if (listen(fd, 1) < 0) {
        fprintf(stderr, "listen failed\n");
        close(fd);
        return -1;
    }
    return fd;
}

int HcclAllReduceTest(DeviceContext &ctx, const char *label) {
    uint64_t count = kHcclDataSize;
    float *host_buf = nullptr;
    CHECK_ACL(aclrtMallocHost(&host_buf, count * sizeof(float)));
    
    for (uint64_t i = 0; i < count; ++i) {
        host_buf[i] = static_cast<float>(ctx.device_id);
    }
    CHECK_ACL(aclrtMemcpy(ctx.send_buf, count * sizeof(float), host_buf, count * sizeof(float), ACL_MEMCPY_HOST_TO_DEVICE));
    
    HCCLCHECK(HcclAllReduce(ctx.send_buf, ctx.recv_buf, count, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.hccl_comm, ctx.stream));
    CHECK_ACL(aclrtSynchronizeStream(ctx.stream));
    
    float *result_buf = nullptr;
    CHECK_ACL(aclrtMallocHost(&result_buf, count * sizeof(float)));
    CHECK_ACL(aclrtMemcpy(result_buf, count * sizeof(float), ctx.recv_buf, count * sizeof(float), ACL_MEMCPY_DEVICE_TO_HOST));
    
    float expected = 0.0f;
    for (int i = 0; i < g_dev_count; ++i) {
        expected += static_cast<float>(i);
    }
    
    bool success = true;
    for (uint64_t i = 0; i < count; ++i) {
        if (result_buf[i] != expected) {
            success = false;
            break;
        }
    }
    
    printf("[Device %d] %s: %s (expected=%f, got=%f)\n", ctx.device_id, label, success ? "PASS" : "FAIL", expected, result_buf[0]);
    
    CHECK_ACL(aclrtFreeHost(host_buf));
    CHECK_ACL(aclrtFreeHost(result_buf));
    return success ? 0 : -1;
}

int HcclInit(int device_id, int dev_count, const HcclRootInfo &root_info) {
    CHECK_ACL(aclrtSetDevice(device_id));
    
    DeviceContext ctx{};
    ctx.device_id = device_id;
    ctx.hcc_initialized = false;
    
    size_t buf_size = kHcclDataSize * sizeof(float);
    CHECK_ACL(aclrtMalloc(&ctx.send_buf, buf_size, ACL_MEM_MALLOC_HUGE_ONLY));
    CHECK_ACL(aclrtMalloc(&ctx.recv_buf, buf_size, ACL_MEM_MALLOC_HUGE_ONLY));
    CHECK_ACL(aclrtCreateStream(&ctx.stream));
    
    HcclCommConfig config;
    HcclCommConfigInit(&config);
    std::strcpy(config.hcclCommName, "hcc_test_comm");
    
    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfoConfig(dev_count, &root_info, device_id, &config, &comm));
    ctx.hccl_comm = comm;
    ctx.hcc_initialized = true;
    
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_device_ctxs[device_id] = ctx;
    }
    
    g_hcc_initialized_count++;
    printf("[Device %d] HCCL comm initialized\n", device_id);
    return 0;
}

void HcclThread(int device_id, int dev_count, const HcclRootInfo &root_info) {
    if (HcclInit(device_id, dev_count, root_info) != 0) {
        fprintf(stderr, "[Device %d] HcclInit failed\n", device_id);
        return;
    }
    
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, []{ return g_hcc_initialized_count == g_dev_count; });
    lock.unlock();
    
    HcclAllReduceTest(g_device_ctxs[device_id], "HCCL AllReduce BEFORE HIXL");
    
    lock.lock();
    g_cv.wait(lock, []{ return g_hixl_ready; });
    lock.unlock();
    
    HcclAllReduceTest(g_device_ctxs[device_id], "HCCL AllReduce AFTER HIXL");
}

int InitHixlEngine(HixlContext &ctx, const std::string &local_engine, const std::string &remote_engine) {
    CHECK_ACL(aclrtSetDevice(ctx.device_id));
    
    std::map<AscendString, AscendString> options;
    std::string resource_config = "{\"comm_resource_config.protocol_desc\": [\"roce:device\"]}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
    
    HIXLCHECK(ctx.engine.Initialize(local_engine.c_str(), options));
    ctx.initialized = true;
    
    uint8_t *dev_ptr = nullptr;
    CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kHixlBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ctx.dev_buf = dev_ptr;
    
    MemDesc desc{};
    desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
    desc.len = kHixlBufSize;
    HIXLCHECK(ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle));
    
    printf("[HIXL] InitEngine success, device:%d, local:%s, remote:%s\n", ctx.device_id, local_engine.c_str(), remote_engine.c_str());
    return 0;
}

int HixlTransfer(HixlContext &client, HixlContext &server) {
    std::vector<uint8_t> fill_data(kHixlBufSize, kFillValue);
    CHECK_ACL(aclrtMemcpy(server.dev_buf, kHixlBufSize, fill_data.data(), kHixlBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
    
    HIXLCHECK(client.engine.Connect(server.local_engine.c_str(), 5000));
    client.connected = true;
    
    std::vector<TransferOpDesc> descs;
    constexpr size_t kBlockSize = 16 * 1024;
    size_t kBlockCount = kHixlBufSize / kBlockSize;
    for (size_t i = 0; i < kBlockCount; ++i) {
        TransferOpDesc desc{};
        desc.local_addr = reinterpret_cast<uintptr_t>(client.dev_buf) + i * kBlockSize;
        desc.remote_addr = reinterpret_cast<uintptr_t>(server.dev_buf) + i * kBlockSize;
        desc.len = kBlockSize;
        descs.push_back(desc);
    }
    
    HIXLCHECK(client.engine.TransferSync(server.local_engine.c_str(), READ, descs, 30000));
    printf("[HIXL] TransferSync READ completed\n");
    
    void *host_buf = nullptr;
    CHECK_ACL(aclrtMallocHost(&host_buf, kHixlBufSize));
    CHECK_ACL(aclrtMemcpy(host_buf, kHixlBufSize, client.dev_buf, kHixlBufSize, ACL_MEMCPY_DEVICE_TO_HOST));
    
    bool success = true;
    for (size_t i = 0; i < kHixlBufSize; ++i) {
        if (static_cast<uint8_t *>(host_buf)[i] != kFillValue) {
            success = false;
            break;
        }
    }
    printf("[HIXL] Verify: %s\n", success ? "PASS" : "FAIL");
    
    CHECK_ACL(aclrtFreeHost(host_buf));
    return success ? 0 : -1;
}

void FinalizeHixl(HixlContext &ctx) {
    if (ctx.connected) {
        ctx.engine.Disconnect(ctx.remote_engine.c_str(), 5000);
    }
    if (ctx.dev_handle != nullptr) {
        ctx.engine.DeregisterMem(ctx.dev_handle);
    }
    if (ctx.dev_buf != nullptr) {
        aclrtFree(ctx.dev_buf);
    }
    if (ctx.initialized) {
        ctx.engine.Finalize();
    }
}

void FinalizeHccl(DeviceContext &ctx) {
    if (ctx.hcc_initialized) {
        HcclCommDestroy(ctx.hccl_comm);
    }
    if (ctx.stream != nullptr) {
        aclrtDestroyStream(ctx.stream);
    }
    if (ctx.send_buf != nullptr) {
        aclrtFree(ctx.send_buf);
    }
    if (ctx.recv_buf != nullptr) {
        aclrtFree(ctx.recv_buf);
    }
    aclrtResetDevice(ctx.device_id);
}

int main(int argc, char **argv) {
    printf("=== HIXL + HCCL Coexistence Test ===\n");
    
    CHECK_ACL(aclInit(NULL));
    CHECK_ACL(aclrtGetDeviceCount(reinterpret_cast<uint32_t *>(&g_dev_count)));
    
    if (g_dev_count < 2) {
        fprintf(stderr, "Need at least 2 devices for this test\n");
        CHECK_ACL(aclFinalize());
        return -1;
    }
    
    printf("Found %d NPU device(s)\n", g_dev_count);
    g_device_ctxs.resize(g_dev_count);
    
    int root_rank = 0;
    CHECK_ACL(aclrtSetDevice(root_rank));
    
    HcclRootInfo root_info;
    HCCLCHECK(HcclGetRootInfo(&root_info));
    
    std::vector<std::thread> threads;
    for (int i = 0; i < g_dev_count; ++i) {
        threads.emplace_back(HcclThread, i, g_dev_count, std::cref(root_info));
    }
    
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, []{ return g_hcc_initialized_count == g_dev_count; });
    printf("All devices completed HCCL init\n");
    
    g_hixl_client.device_id = 0;
    g_hixl_client.local_engine = "127.0.0.1:16000";
    g_hixl_client.remote_engine = "127.0.0.1:16001";
    
    g_hixl_server.device_id = (g_dev_count > 1) ? 1 : 0;
    g_hixl_server.local_engine = "127.0.0.1:16001";
    g_hixl_server.remote_engine = "127.0.0.1:16000";
    
    printf("Initializing HIXL on device %d (client) and %d (server)\n", g_hixl_client.device_id, g_hixl_server.device_id);
    
    if (InitHixlEngine(g_hixl_server, g_hixl_server.local_engine, g_hixl_server.remote_engine) != 0) {
        fprintf(stderr, "HIXL server init failed\n");
        goto cleanup;
    }
    
    if (InitHixlEngine(g_hixl_client, g_hixl_client.local_engine, g_hixl_client.remote_engine) != 0) {
        fprintf(stderr, "HIXL client init failed\n");
        goto cleanup;
    }
    
    if (HixlTransfer(g_hixl_client, g_hixl_server) != 0) {
        fprintf(stderr, "HIXL transfer failed\n");
        goto cleanup;
    }
    
    printf("HIXL communication completed\n");
    
    g_hixl_ready = true;
    lock.unlock();
    g_cv.notify_all();
    
    for (auto &t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    printf("=== All tests completed ===\n");
    
cleanup:
    FinalizeHixl(g_hixl_client);
    FinalizeHixl(g_hixl_server);
    
    for (auto &ctx : g_device_ctxs) {
        FinalizeHccl(ctx);
    }
    
    CHECK_ACL(aclFinalize());
    return 0;
}
