/**
 * @file dsmi_stub.cc
 * @brief DSMI 接口桩函数，用于单元测试环境
 *
 * 提供 dsmi_get_board_info / dsmi_get_device_info 的桩实现，
 * 使 DsmiProxy 通过 dlopen("libdrvdsmi_host.so") 加载的接口在测试环境下可调用。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <cstring>

// ============ dsmi_proxy.cc 中的结构体定义（需保持一致） ============

struct dsmi_board_info_stru {
  unsigned int board_id;
  unsigned int pcb_id;
  unsigned int type_id;
  unsigned int slot_id;
};

// dsmi_get_device_info 命令号（与 dsmi_proxy.cc 对齐）
static const unsigned int kDsmiMainCmdChipInf = 12U;
static const unsigned int kDsmiChipInfSubCmdSpodInfo = 1U;

// ============ 可配置的桩返回值 ============

static unsigned int g_slot_id = 0U;
static int g_board_info_ret = 0;

static unsigned int g_intercon_type = 4U;  // 默认 UBG
static unsigned int g_super_pod_id = 0U;
static int g_device_info_ret = 0;

// ============ 桩函数实现 ============

int dsmi_get_board_info(int device_id, struct dsmi_board_info_stru *pboard_info) {
  (void)device_id;
  if (pboard_info == nullptr) {
    return -1;
  }
  if (g_board_info_ret != 0) {
    return g_board_info_ret;
  }
  std::memset(pboard_info, 0, sizeof(*pboard_info));
  pboard_info->slot_id = g_slot_id;
  return 0;
}

int dsmi_get_device_info(unsigned int device_id, unsigned int main_cmd, unsigned int sub_cmd, void *buf,
                         unsigned int *buf_size) {
  (void)device_id;
  if (buf == nullptr || buf_size == nullptr) {
    return -1;
  }
  if (g_device_info_ret != 0) {
    return g_device_info_ret;
  }
  if (main_cmd != kDsmiMainCmdChipInf || sub_cmd != kDsmiChipInfSubCmdSpodInfo) {
    return -1;
  }
  // dsmi_spod_info 布局: sdid, scale_type, super_pod_id, server_id, chassis_id,
  //                      super_pod_type, super_pod_intercon_type, reserve[5]
  static const unsigned int kSpodInfoFields = 12U;  // 7 + 5 reserve
  if (*buf_size < kSpodInfoFields * sizeof(unsigned int)) {
    return -1;
  }
  auto *fields = static_cast<unsigned int *>(buf);
  std::memset(fields, 0, kSpodInfoFields * sizeof(unsigned int));
  fields[2] = g_super_pod_id;            // super_pod_id
  fields[6] = g_intercon_type;           // super_pod_intercon_type
  *buf_size = kSpodInfoFields * sizeof(unsigned int);
  return 0;
}

// ============ 测试控制接口 ============

void DsmiStubSetInterconType(unsigned int type) {
  g_intercon_type = type;
}

void DsmiStubSetSuperPodId(unsigned int id) {
  g_super_pod_id = id;
}

void DsmiStubSetSlotId(unsigned int slot_id, int ret) {
  g_slot_id = slot_id;
  g_board_info_ret = ret;
}

void DsmiStubSetDeviceInfoRet(int ret) {
  g_device_info_ret = ret;
}

#ifdef __cplusplus
}
#endif
