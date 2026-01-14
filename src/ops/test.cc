#include <iostream>
// #include "../hixl/common/hixl_log.h"
#include "hccl/hccl.h"
#include "hccl/hcomm_primitives.h"
// #include "../../include/hixl/hixl_types.h"

int test() {
    uint64_t thread_;
    void* outputMem_addr = malloc(1024);
    int outputMem_size = 1024;
    void* inputMem_addr = malloc(1024);
    int inputMem_size = 1024;
    // HIXL_LOGE(hixl::FAILED, "Test Log");
    HcclResult result = static_cast<HcclResult>(HcommLocalCopyOnThread(thread_, outputMem_addr, inputMem_addr, inputMem_size));
    std::cout << "Test build successful." << std::endl;

    return 0;
}