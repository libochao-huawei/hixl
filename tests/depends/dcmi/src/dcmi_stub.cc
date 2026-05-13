/**
 * @file dcmi_stub.cc
 * @brief DCMI 接口桩函数，用于单元测试环境
 *
 * 提供 dcmiv2_* 系列函数的桩实现，使 local_comm_res_tool.cc 中
 * 通过 dlopen("libdcmi.so") 加载的 DCMI 接口在测试环境下可调用。
 */

#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif

// ============ 可配置的桩返回值 ============

// dcmiv2_init 控制：0=成功，非0=失败
static int g_dcmi_init_ret = 0;

// dcmiv2_get_mainboard_id 控制
static unsigned int g_mainboard_id = 0x3;  // 默认 Pod1
static int g_mainboard_id_ret = 0;

// dcmiv2_get_dev_id_by_chip_phy_id 控制
static unsigned int g_logic_id = 0;
static int g_logicid_ret = 0;

// dcmiv2_get_urma_device_cnt 控制
static unsigned int g_urma_device_cnt = 1;
static int g_urma_device_cnt_ret = 0;

// dcmiv2_get_device_info (SPOD) 控制
static unsigned int g_super_pod_id = 0;
static int g_device_info_ret = 0;

// dcmiv2_get_eid_list_by_urma_dev_index 控制：返回 EID 数量（0=不返回, 1=仅非PG, 2=全部）
static int g_eid_count = 2;

// ============ 桩函数实现 ============

int dcmiv2_init(void) {
    return g_dcmi_init_ret;
}

int dcmiv2_get_mainboard_id(int npu_id, unsigned int *mainboard_id) {
    (void)npu_id;
    if (mainboard_id == nullptr) {
        return -1;
    }
    if (g_mainboard_id_ret != 0) {
        return g_mainboard_id_ret;
    }
    *mainboard_id = g_mainboard_id;
    return 0;
}

int dcmiv2_get_dev_id_by_chip_phy_id(unsigned int phy_id, unsigned int *logic_id) {
    (void)phy_id;
    if (logic_id == nullptr) {
        return -1;
    }
    if (g_logicid_ret != 0) {
        return g_logicid_ret;
    }
    *logic_id = g_logic_id;
    return 0;
}

int dcmiv2_get_dev_id_from_chip_phyid(unsigned int phy_id, unsigned int *logic_id) {
    return dcmiv2_get_dev_id_by_chip_phy_id(phy_id, logic_id);
}

int dcmiv2_get_urma_device_cnt(int npu_id, unsigned int *dev_cnt) {
    (void)npu_id;
    if (dev_cnt == nullptr) {
        return -1;
    }
    if (g_urma_device_cnt_ret != 0) {
        return g_urma_device_cnt_ret;
    }
    *dev_cnt = g_urma_device_cnt;
    return 0;
}

// 构建默认 EID 数据（16字节/个）
// EID 格式参考 route.conf: 0x000000000000008000100000dfdf00f2
// byte6[0xf2]: high=0xf(>4 → die_id=1), low=0x2 (port=2)
// byte6[0x72]: high=0x7(>=4 → die_id=1, ==7 → PG EID), low=0x2
static void BuildDefaultEid(unsigned char *eid, unsigned char byte15) {
    for (int i = 0; i < 16; ++i) {
        eid[i] = 0x00;
    }
    eid[7] = 0x80;
    eid[9] = 0x10;
    eid[12] = 0xdf;
    eid[13] = 0xdf;
    eid[15] = byte15;
}

int dcmiv2_get_eid_list_by_urma_dev_index(int npu_id, int urma_dev_index,
                                           void *eid_list, int *eid_cnt) {
    (void)npu_id;
    (void)urma_dev_index;
    if (eid_list == nullptr || eid_cnt == nullptr) {
        return -1;
    }
    // 返回 2 个测试 EID
    // eid_list 是 dcmi_urma_eid_info_t 数组，每个包含 16字节 eid + 4字节 index
    // 布局: [16字节 raw][4字节 eid_index]，总共 20 字节/元素
    struct EidInfoRaw {
        unsigned char eid[16];
        unsigned int eid_index;
    };
    auto *infos = static_cast<EidInfoRaw *>(eid_list);
    if (g_eid_count >= 1 && *eid_cnt >= 1) {
        BuildDefaultEid(infos[0].eid, 0xf2);
        infos[0].eid_index = 0;
    }
    if (g_eid_count >= 2 && *eid_cnt >= 2) {
        BuildDefaultEid(infos[1].eid, 0x72);
        infos[1].eid_index = 1;
    }
    *eid_cnt = (g_eid_count < 2) ? g_eid_count : 2;
    return 0;
}

int dcmiv2_get_device_info(int npu_id, int main_cmd, unsigned int sub_cmd,
                            void *buf, unsigned int *size) {
    (void)npu_id;
    (void)main_cmd;
    (void)sub_cmd;
    if (buf == nullptr || size == nullptr) {
        return -1;
    }
    if (g_device_info_ret != 0) {
        return g_device_info_ret;
    }
    // 填充 SPOD 信息（DcmiSpodInfo 结构体）
    // 结构体布局: sdid, super_pod_size, super_pod_id, server_index, chassis_id, super_pod_type, reserve[6]
    if (*size >= 10 * sizeof(unsigned int)) {
        auto *fields = static_cast<unsigned int *>(buf);
        for (unsigned int i = 0; i < *size / sizeof(unsigned int); ++i) {
            fields[i] = 0;
        }
        fields[2] = g_super_pod_id;  // super_pod_id
    }
    return 0;
}

// ============ 测试控制接口 ============

void DcmiStubSetInitRet(int ret) {
    g_dcmi_init_ret = ret;
}

void DcmiStubSetMainboardId(unsigned int id, int ret) {
    g_mainboard_id = id;
    g_mainboard_id_ret = ret;
}

void DcmiStubSetLogicId(unsigned int id, int ret) {
    g_logic_id = id;
    g_logicid_ret = ret;
}

void DcmiStubSetUrmaDeviceCnt(unsigned int cnt, int ret) {
    g_urma_device_cnt = cnt;
    g_urma_device_cnt_ret = ret;
}

void DcmiStubSetSuperPodId(unsigned int id, int ret) {
    g_super_pod_id = id;
    g_device_info_ret = ret;
}

void DcmiStubSetEidCount(int count) {
    g_eid_count = count;
}

#ifdef __cplusplus
}
#endif
