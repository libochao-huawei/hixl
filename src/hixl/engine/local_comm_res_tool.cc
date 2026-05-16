/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes 生成工具实现文件
 *
 * 本文件负责：
 * - DCMI 接口封装（GetUBEntityList, GetMainboardId）
 * - 文件解析（ParseTopoFile, ParseRouteFile）
 * - 边生成（GenerateD2DEdges, GenerateH2DEdges, GenerateD2HEdges）
 * - 核心流程（GenerateLocalCommRes）
 *
 * RootInfo 构建功能已拆分到 rootinfo_builder 模块
 */

#include "local_comm_res_tool.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include "common/hixl_log.h"

namespace hixl {

// ============ 产品形态常量 ============

namespace {
constexpr uint32_t kMainboardIdPod1 = 0x3;
constexpr uint32_t kMainboardIdPod2 = 0x5;
constexpr uint32_t kMainboardIdPod3 = 0x7;
constexpr uint32_t kMainboardIdServerMin1 = 0x21;
constexpr uint32_t kMainboardIdServerMax1 = 0x2B;
constexpr uint32_t kMainboardIdServerMin2 = 0x40;
constexpr uint32_t kMainboardIdServerMax2 = 0x46;

// Topology 常量
constexpr const char *kLinkTypePeer2Peer = "PEER2PEER";
constexpr const char *kTopoType1DMesh = "1DMESH";

// Plane 常量
constexpr const char *kPlanePg0 = "plane_pg_0";
constexpr const char *kPlanePg1 = "plane_pg_1";

// 网络实例前缀
constexpr const char *kNetInstancePrefix = "superpod_";

// 默认路径常量
constexpr const char *kDefaultTopoDir = "/etc/";
constexpr const char *kDefaultTopoSuffix = "noroce.json";
constexpr const char *kDefaultRoutePath = "/lib/route.conf";
}  // anonymous namespace

// ============ DCMI 接口动态加载 ============

namespace {

// DCMI SPOD 信息结构体
struct DcmiSpodInfo {
  unsigned int sdid;
  unsigned int super_pod_size;
  unsigned int super_pod_id;
  unsigned int server_index;
  unsigned int chassis_id;
  unsigned int super_pod_type;
  unsigned int reserve[6];
};

// DCMI 主命令和子命令
enum DcmiMainCmd {
  DCMI_MAIN_CMD_CHIP_INF = 12,
};

enum DcmiChipInfoSubCmd {
  DCMI_CHIP_INFO_SUB_CMD_SPOD_INFO = 1,
};

/**
 * @brief 获取 logic ID 从 phy ID
 */
int GetLogicIdFromPhyId(unsigned int phy_id, unsigned int *logic_id) {
  if (LoadDcmi() != 0) {
    return -1;
  }
  return g_dcmi_get_logicid_from_phyid(phy_id, logic_id);
}

// ============ JSON 解析辅助函数 ============

bool FindJsonColon(const std::string &obj, const std::string &key, size_t &value_pos) {
  std::string pattern = "\"" + key + "\"";
  size_t pos = obj.find(pattern);
  if (pos == std::string::npos) {
    return false;
  }
  pos = obj.find(':', pos + pattern.length());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < obj.length() && std::isspace(static_cast<unsigned char>(obj[pos]))) {
    ++pos;
  }
  value_pos = pos;
  return true;
}

bool ParseJsonFieldInt(const std::string &obj, const std::string &key, int32_t &value) {
  size_t pos = 0;
  if (!FindJsonColon(obj, key, pos)) {
    return false;
  }
  size_t end = pos;
  while (end < obj.length() && (std::isdigit(static_cast<unsigned char>(obj[end])) || obj[end] == '-')) {
    ++end;
  }
  if (end == pos) {
    return false;
  }
  value = std::stoi(obj.substr(pos, end - pos));
  return true;
}

bool ParseJsonFieldString(const std::string &obj, const std::string &key, std::string &value) {
  size_t pos = 0;
  if (!FindJsonColon(obj, key, pos)) {
    return false;
  }
  if (pos >= obj.length() || obj[pos] != '"') {
    return false;
  }
  ++pos;
  size_t end = obj.find('"', pos);
  if (end == std::string::npos) {
    return false;
  }
  value = obj.substr(pos, end - pos);
  return true;
}

bool ParseJsonFieldStringArray(const std::string &obj, const std::string &key, std::vector<std::string> &values) {
  std::string pattern = "\"" + key + "\"";
  size_t pos = obj.find(pattern);
  if (pos == std::string::npos) {
    return false;
  }
  pos = obj.find('[', pos + pattern.length());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  values.clear();
  int bracket_depth = 1;
  std::string current_str;
  bool in_string = false;
  while (pos < obj.length() && bracket_depth > 0) {
    if (obj[pos] == '[') {
      ++bracket_depth;
    } else if (obj[pos] == ']') {
      --bracket_depth;
      if (bracket_depth == 0) break;
    } else if (obj[pos] == '"' && !in_string) {
      in_string = true;
    } else if (obj[pos] == '"' && in_string) {
      values.push_back(current_str);
      current_str.clear();
      in_string = false;
    } else if (in_string) {
      current_str += obj[pos];
    }
    ++pos;
  }
  return true;
}

std::vector<std::string> ExtractJsonObjects(const std::string &json, const std::string &array_key) {
  std::vector<std::string> objects;
  size_t array_pos = json.find("\"" + array_key + "\"");
  if (array_pos == std::string::npos) {
    return objects;
  }
  size_t bracket_pos = json.find('[', array_pos);
  if (bracket_pos == std::string::npos) {
    return objects;
  }
  size_t pos = bracket_pos + 1;
  int brace_depth = 0;
  size_t obj_start = std::string::npos;
  while (pos < json.length()) {
    if (json[pos] == '{') {
      if (brace_depth == 0) {
        obj_start = pos;
      }
      ++brace_depth;
    } else if (json[pos] == '}') {
      --brace_depth;
      if (brace_depth == 0 && obj_start != std::string::npos) {
        objects.push_back(json.substr(obj_start, pos - obj_start + 1));
        obj_start = std::string::npos;
      }
    }
    ++pos;
  }
  return objects;
}

// 在 /etc/ 目录下模糊匹配以 noroce.json 结尾的文件，返回修改时间最新的一个
std::string FindLatestTopoFile() {
  DIR *dir = opendir(kDefaultTopoDir);
  if (!dir) {
    HIXL_LOGW("Failed to open /etc/ for topo file scan");
    return "";
  }

  std::string latest_file;
  time_t latest_mtime = 0;
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name(entry->d_name);
    if (name.size() < std::strlen(kDefaultTopoSuffix)) {
      continue;
    }
    if (name.compare(name.size() - std::strlen(kDefaultTopoSuffix), std::strlen(kDefaultTopoSuffix), kDefaultTopoSuffix) !=
        0) {
      continue;
    }

    std::string full_path = std::string(kDefaultTopoDir) + name;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (st.st_mtime > latest_mtime) {
        latest_mtime = st.st_mtime;
        latest_file = full_path;
      }
    }
  }
  closedir(dir);
  return latest_file;
}

}  // anonymous namespace

// ============ 文件解析辅助函数 ============

bool LoadRouteKvMap(std::ifstream &file, std::map<std::string, std::string> &kv_map) {
  std::string line;
  while (std::getline(file, line)) {
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    size_t key_end = key.find_last_not_of(" \t\r\n");
    if (key_end != std::string::npos) key = key.substr(0, key_end + 1);

    size_t val_start = value.find_first_not_of(" \t\r\n");
    size_t val_end = value.find_last_not_of(" \t\r\n");
    if (val_start != std::string::npos && val_end != std::string::npos) {
      value = value.substr(val_start, val_end - val_start + 1);
    }
    kv_map[key] = value;
  }
  return true;
}

void AddRouteEntriesForDevice(const std::map<std::string, std::string> &kv_map, int device_idx, int device_id,
                              RouteData &route_data) {
  std::string chan_num_key = "pair" + std::to_string(device_idx) + "_chan_num";
  int chan_num = 1;
  auto chan_num_it = kv_map.find(chan_num_key);
  if (chan_num_it != kv_map.end()) {
    chan_num = std::stoi(chan_num_it->second);
  }

  for (int j = 0; j < chan_num; ++j) {
    std::string local_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_local_eid";
    std::string remote_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_remote_eid";

    auto local_it = kv_map.find(local_key);
    auto remote_it = kv_map.find(remote_key);

    if (local_it != kv_map.end() && remote_it != kv_map.end()) {
      RouteEntry entry;
      entry.device_id = device_id;
      entry.local_eid = local_it->second;
      entry.remote_eid = remote_it->second;
      // 去掉 "0x" 前缀
      if (entry.local_eid.size() >= 2 && entry.local_eid[0] == '0' && entry.local_eid[1] == 'x') {
        entry.local_eid = entry.local_eid.substr(2);
      }
      if (entry.remote_eid.size() >= 2 && entry.remote_eid[0] == '0' && entry.remote_eid[1] == 'x') {
        entry.remote_eid = entry.remote_eid.substr(2);
      }
      route_data.entries.push_back(entry);
    }
  }
}

int32_t BuildRouteEntries(const std::map<std::string, std::string> &kv_map, RouteData &route_data) {
  auto it = kv_map.find("pair_device_num");
  if (it == kv_map.end()) {
    HIXL_LOGE(FAILED, "Missing pair_device_num");
    return FAILED;
  }

  int pair_device_num = std::stoi(it->second);
  for (int i = 0; i < pair_device_num; ++i) {
    std::string dev_id_key = "pair" + std::to_string(i) + "_dev_id";
    auto dev_it = kv_map.find(dev_id_key);
    if (dev_it == kv_map.end()) {
      continue;
    }

    int device_id = std::stoi(dev_it->second);
    AddRouteEntriesForDevice(kv_map, i, device_id, route_data);
  }

  HIXL_LOGI("Parsed %zu route entries", route_data.entries.size());
  return SUCCESS;
}

// ============ DCMI 接口封装实现 ============

int32_t GetMainboardId(int32_t phy_dev_id, unsigned int &mainboard_id) {
  if (LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  unsigned int logic_id = 0;
  if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from phy id: %d", phy_dev_id);
    return FAILED;
  }

  int ret = g_dcmi_get_mainboard_id(logic_id, &mainboard_id);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get mainboard id, ret=%d", ret);
    return FAILED;
  }

  return SUCCESS;
}

int32_t GetClosNetInstanceId(int32_t phy_dev_id, std::string &net_instance_id) {
  if (LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  unsigned int logic_id = 0;
  if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from phy id: %d", phy_dev_id);
    return FAILED;
  }

  DcmiSpodInfo spod_info = {};
  unsigned int buf_size = sizeof(DcmiSpodInfo);
  int ret =
      g_dcmi_get_device_info(logic_id, DCMI_MAIN_CMD_CHIP_INF, DCMI_CHIP_INFO_SUB_CMD_SPOD_INFO, &spod_info, &buf_size);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get device info, ret=%d", ret);
    return FAILED;
  }

  net_instance_id = std::string(kNetInstancePrefix) + std::to_string(spod_info.super_pod_id);
  HIXL_LOGI("phy_dev_id=%d, super_pod_id=%u, net_instance_id=%s", phy_dev_id, spod_info.super_pod_id,
            net_instance_id.c_str());
  return SUCCESS;
}

// ============ 文件解析实现 ============

int32_t ParseTopoFile(const std::string &topo_path, TopoData &topo_data) {
  topo_data.links.clear();
  std::ifstream file(topo_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open topo file: %s", topo_path.c_str());
    return PARAM_INVALID;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  file.close();

  if (content.empty()) {
    HIXL_LOGE(FAILED, "Topo file is empty: %s", topo_path.c_str());
    return FAILED;
  }

  std::vector<std::string> edge_objects = ExtractJsonObjects(content, "edge_list");
  if (edge_objects.empty()) {
    HIXL_LOGE(FAILED, "No edge_list found in: %s", topo_path.c_str());
    return FAILED;
  }

  for (const auto &obj : edge_objects) {
    TopoLink link;
    link.remote_a = -1;
    link.remote_b = -1;

    if (!ParseJsonFieldInt(obj, "net_layer", link.net_layer)) {
      HIXL_LOGW("Missing net_layer in edge object, skipping");
      continue;
    }
    ParseJsonFieldString(obj, "link_type", link.link_type);
    ParseJsonFieldString(obj, "topo_type", link.topo_type);
    ParseJsonFieldInt(obj, "local_a", link.local_a);
    ParseJsonFieldInt(obj, "local_b", link.local_b);
    ParseJsonFieldInt(obj, "remote_a", link.remote_a);
    ParseJsonFieldInt(obj, "remote_b", link.remote_b);
    ParseJsonFieldStringArray(obj, "local_a_ports", link.local_a_ports);
    ParseJsonFieldStringArray(obj, "local_b_ports", link.local_b_ports);

    topo_data.links.push_back(link);
  }

  HIXL_LOGI("Parsed %zu links from %s", topo_data.links.size(), topo_path.c_str());
  return SUCCESS;
}

int32_t ParseRouteFile(const std::string &route_path, RouteData &route_data) {
  route_data.entries.clear();
  std::ifstream file(route_path);
  if (!file.is_open()) {
    HIXL_LOGE(PARAM_INVALID, "Failed to open route file: %s", route_path.c_str());
    return PARAM_INVALID;
  }

  std::map<std::string, std::string> kv_map;
  if (!LoadRouteKvMap(file, kv_map)) {
    HIXL_LOGE(FAILED, "Failed to load route kv map");
    return FAILED;
  }
  file.close();

  return BuildRouteEntries(kv_map, route_data);
}

// ============ 边生成实现 ============

bool ShouldSkipD2DLink(const TopoLink &link, size_t skip_reason[4]) {
  if (link.net_layer != 0) {
    ++skip_reason[0];
    return true;
  }
  if (link.link_type != kLinkTypePeer2Peer) {
    ++skip_reason[1];
    return true;
  }
  if (link.topo_type != kTopoType1DMesh) {
    ++skip_reason[2];
    return true;
  }
  return false;
}

void AddD2DEdgesFromLink(const NpuRootInfo &self_rootinfo, const NpuRootInfo &peer_rootinfo, int peer_id,
                         const std::vector<std::string> &local_ports, const std::vector<std::string> &peer_ports,
                         std::vector<EndpointConfig> &edges, size_t &no_port_match_local, size_t &no_port_match_peer) {
  for (size_t i = 0; i < local_ports.size() && i < peer_ports.size(); ++i) {
    const std::string &local_port = local_ports[i];
    const std::string &peer_port = peer_ports[i];

    auto local_eid_it = self_rootinfo.port_to_eid.find(local_port);
    if (local_eid_it == self_rootinfo.port_to_eid.end()) {
      ++no_port_match_local;
      HIXL_LOGD("No EID for local port '%s'", local_port.c_str());
      continue;
    }
    std::string comm_id = local_eid_it->second;

    auto peer_eid_it = peer_rootinfo.port_to_eid.find(peer_port);
    if (peer_eid_it == peer_rootinfo.port_to_eid.end()) {
      ++no_port_match_peer;
      HIXL_LOGD("No EID for peer port '%s' on npu_id=%d", peer_port.c_str(), peer_id);
      continue;
    }
    std::string dst_eid = peer_eid_it->second;

    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = comm_id;
    edge.placement = kPlacementDevice;
    edge.dst_eid = dst_eid;
    edges.push_back(edge);
    HIXL_LOGD("D2D matched: comm_id=%s, dst_eid=%s", comm_id.c_str(), dst_eid.c_str());
  }
}

int32_t GenerateD2DEdges(const TopoData &topo_data, const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_id,
                         std::vector<EndpointConfig> &edges) {
  edges.clear();

  auto self_it = npu_rootinfos.find(phy_id);
  if (self_it == npu_rootinfos.end()) {
    HIXL_LOGW("No rootinfo for self npu_id=%d", phy_id);
    return SUCCESS;
  }
  const auto &self_rootinfo = self_it->second;

  HIXL_LOGI("D2D: phy_id=%d, topo_links=%zu, self_rootinfo_size=%zu", phy_id, topo_data.links.size(),
            self_rootinfo.port_to_eid.size());

  size_t skip_reason[4] = {0, 0, 0, 0};
  size_t no_rootinfo_peer = 0;
  size_t no_port_match_local = 0;
  size_t no_port_match_peer = 0;

  for (const auto &link : topo_data.links) {
    if (ShouldSkipD2DLink(link, skip_reason)) {
      continue;
    }

    bool is_local_a_side = (link.local_a == phy_id);
    bool is_local_b_side = (link.local_b == phy_id);
    if (!is_local_a_side && !is_local_b_side) {
      ++skip_reason[3];
      continue;
    }

    int peer_id = is_local_a_side ? link.local_b : link.local_a;
    const std::vector<std::string> &local_ports = is_local_a_side ? link.local_a_ports : link.local_b_ports;
    const std::vector<std::string> &peer_ports = is_local_a_side ? link.local_b_ports : link.local_a_ports;

    if (local_ports.empty() || peer_ports.empty()) {
      HIXL_LOGD("D2D skip (empty ports)");
      continue;
    }

    auto peer_it = npu_rootinfos.find(peer_id);
    if (peer_it == npu_rootinfos.end()) {
      ++no_rootinfo_peer;
      HIXL_LOGD("D2D No rootinfo for peer npu_id=%d", peer_id);
      continue;
    }
    const auto &peer_rootinfo = peer_it->second;

    AddD2DEdgesFromLink(self_rootinfo, peer_rootinfo, peer_id, local_ports, peer_ports, edges, no_port_match_local,
                        no_port_match_peer);
  }

  HIXL_LOGI(
      "D2D result: matched=%zu, skip(net_layer)=%zu, skip(link_type)=%zu, "
      "skip(topo_type)=%zu, skip(phy_id)=%zu, no_port_match_local=%zu, "
      "no_port_match_peer=%zu, no_rootinfo_peer=%zu",
      edges.size(), skip_reason[0], skip_reason[1], skip_reason[2], skip_reason[3], no_port_match_local,
      no_port_match_peer, no_rootinfo_peer);
  return SUCCESS;
}

int32_t GenerateH2DEdges(const RouteData &route_data, std::vector<EndpointConfig> &edges) {
  edges.clear();

  HIXL_LOGI("H2D: route_entries=%zu", route_data.entries.size());

  for (const auto &entry : route_data.entries) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = entry.local_eid;
    edge.placement = kPlacementHost;
    edge.dst_eid = entry.remote_eid;
    edges.push_back(edge);
    HIXL_LOGD("H2D matched: device_id=%d, local_eid=%s, remote_eid=%s", entry.device_id, entry.local_eid.c_str(),
              entry.remote_eid.c_str());
  }

  HIXL_LOGI("H2D result: matched=%zu", edges.size());
  return SUCCESS;
}

int32_t GenerateD2HEdges(const RouteData &route_data, int32_t phy_dev_id, std::vector<EndpointConfig> &edges) {
  edges.clear();

  HIXL_LOGI("D2H: route_entries=%zu, phy_dev_id=%d", route_data.entries.size(), phy_dev_id);

  for (const auto &entry : route_data.entries) {
    if (entry.device_id != (phy_dev_id % 8)) {
      continue;
    }
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = entry.remote_eid;
    edge.placement = kPlacementDevice;
    edge.dst_eid = entry.local_eid;
    edges.push_back(edge);
    HIXL_LOGD("D2H matched: device_id=%d, remote_eid=%s, local_eid=%s", entry.device_id, entry.remote_eid.c_str(),
              entry.local_eid.c_str());
  }

  HIXL_LOGI("D2H result: matched=%zu", edges.size());
  return SUCCESS;
}

// ============ 核心接口实现 ============

void LogEndpointList(const std::vector<EndpointConfig> &endpoint_list) {
  for (size_t i = 0; i < endpoint_list.size(); ++i) {
    const auto &ep = endpoint_list[i];
    HIXL_LOGI("  [%zu] protocol=%s, comm_id=%s, placement=%s, plane=%s, dst_eid=%s, net_instance_id=%s", i,
              ep.protocol.c_str(), ep.comm_id.c_str(), ep.placement.c_str(), ep.plane.c_str(), ep.dst_eid.c_str(),
              ep.net_instance_id.c_str());
  }
}

bool IsProductPod(uint32_t mainboard_id) {
  return (mainboard_id == kMainboardIdPod1 || mainboard_id == kMainboardIdPod2 || mainboard_id == kMainboardIdPod3);
}

bool IsProductServer(uint32_t mainboard_id) {
  return (
      (mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 && (mainboard_id % 2 == 1)) ||
      (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 && (mainboard_id % 2 == 0)));
}

std::set<int32_t> CollectRelatedNpuIds(int32_t phy_dev_id, const TopoData &topo_data) {
  (void)topo_data;
  // NPU 按 8 个一组划分，找出 phy_dev_id 所在组的所有 NPU
  int32_t group_start = (phy_dev_id / 8) * 8;
  std::set<int32_t> related_npu_ids;
  for (int32_t i = 0; i < 8; ++i) {
    related_npu_ids.insert(group_start + i);
  }
  std::string npu_ids_str;
  for (int id : related_npu_ids) {
    npu_ids_str += std::to_string(id) + " ";
  }
  HIXL_LOGI("phy_dev_id=%d, group_start=%d, Related NPU IDs: %s", phy_dev_id, group_start, npu_ids_str.c_str());
  return related_npu_ids;
}

int32_t BuildNpuRootinfos(const std::set<int32_t> &related_npu_ids, bool is_server,
                          std::map<int32_t, NpuRootInfo> &npu_rootinfos) {
  npu_rootinfos.clear();
  for (int32_t npu_id : related_npu_ids) {
    NpuRootInfo rootinfo;
    int32_t build_ret = BuildNpuRootInfo(npu_id, is_server, rootinfo);
    if (build_ret != SUCCESS) {
      HIXL_LOGE(FAILED, "Failed to build rootinfo for npu_id=%d, ret=%d", npu_id, build_ret);
      return build_ret;
    }
    npu_rootinfos[npu_id] = rootinfo;
  }
  HIXL_LOGI("Built rootinfo for %zu NPU(s)", npu_rootinfos.size());
  return SUCCESS;
}

void CollectClosPgEids(const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_dev_id, bool is_server,
                       std::string &plane_pg_0_eid, std::string &plane_pg_1_eid, int &mesh_die_id) {
  mesh_die_id = GetMeshDieId(phy_dev_id, is_server);
  auto self_it = npu_rootinfos.find(phy_dev_id);
  if (self_it == npu_rootinfos.end()) {
    return;
  }

  // rootinfo_builder 已按正确逻辑过滤：clos_pg_eids[0]=plane_pg_0, [1]=plane_pg_1
  const auto &pg_eids = self_it->second.clos_pg_eids;
  if (!pg_eids.empty()) {
    plane_pg_0_eid = pg_eids[0].eid;
  }
  if (pg_eids.size() >= 2) {
    plane_pg_1_eid = pg_eids[1].eid;
  }
  HIXL_LOGI("Mesh die_id=%d", mesh_die_id);
  HIXL_LOGI("plane_pg_0_eid=%s", plane_pg_0_eid.empty() ? "(none)" : plane_pg_0_eid.c_str());
  HIXL_LOGI("plane_pg_1_eid=%s", plane_pg_1_eid.empty() ? "(none)" : plane_pg_1_eid.c_str());
}

void GenerateD2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                      std::vector<EndpointConfig> &d2u_edges) {
  if (!plane_pg_0_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_0_eid;
    edge.placement = kPlacementDevice;
    edge.plane = kPlanePg0;
    d2u_edges.push_back(edge);
  }
  if (!plane_pg_1_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_1_eid;
    edge.placement = kPlacementDevice;
    edge.plane = kPlanePg1;
    d2u_edges.push_back(edge);
  }
}

void GenerateH2UEdges(const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                      std::vector<EndpointConfig> &h2u_edges) {
  if (!plane_pg_0_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_0_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg0;
    h2u_edges.push_back(edge);
  }
  if (!plane_pg_1_eid.empty()) {
    EndpointConfig edge;
    edge.protocol = kProtocolUbCtp;
    edge.comm_id = plane_pg_1_eid;
    edge.placement = kPlacementHost;
    edge.plane = kPlanePg1;
    h2u_edges.push_back(edge);
  }
}

void CollectAllEdges(const TopoData &topo_data, const RouteData &route_data,
                     const std::map<int32_t, NpuRootInfo> &npu_rootinfos, int32_t phy_dev_id,
                     const std::string &plane_pg_0_eid, const std::string &plane_pg_1_eid,
                     std::vector<EndpointConfig> &all_edges) {
  if (!topo_data.links.empty() && !npu_rootinfos.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateD2DEdges(topo_data, npu_rootinfos, phy_dev_id, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  if (!plane_pg_0_eid.empty() || !plane_pg_1_eid.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateD2UEdges(plane_pg_0_eid, plane_pg_1_eid, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
    edges.clear();
    GenerateH2UEdges(plane_pg_0_eid, plane_pg_1_eid, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
  if (!route_data.entries.empty()) {
    std::vector<EndpointConfig> edges;
    GenerateH2DEdges(route_data, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
    edges.clear();
    GenerateD2HEdges(route_data, phy_dev_id, edges);
    all_edges.insert(all_edges.end(), edges.begin(), edges.end());
  }
}

int32_t GenerateLocalCommRes(int32_t phy_dev_id, LocalCommRes &local_comm_res) {
  // 1. 使用默认路径
  std::string topo_path = FindLatestTopoFile();
  if (topo_path.empty()) {
    HIXL_LOGE(PARAM_INVALID, "No topo file found in /etc/");
    return PARAM_INVALID;
  }
  std::string route_path = kDefaultRoutePath;
  return GenerateLocalCommRes(phy_dev_id, topo_path, route_path, local_comm_res);
}

int32_t GenerateLocalCommRes(int32_t phy_dev_id, const std::string &topo_path, const std::string &route_path,
                             LocalCommRes &local_comm_res) {
  // 1. 获取产品信息
  unsigned int mainboard_id = 0;
  int32_t ret = GetMainboardId(phy_dev_id, mainboard_id);
  if (ret != SUCCESS) {
    return ret;
  }
  bool is_server = IsProductServer(mainboard_id);
  // 3. 解析文件
  TopoData topo_data;
  ret = ParseTopoFile(topo_path, topo_data);
  if (ret != SUCCESS) {
    return ret;
  }
  RouteData route_data;
  ret = ParseRouteFile(route_path, route_data);
  if (ret != SUCCESS) {
    return ret;
  }

  // 4. 构建 NpuRootInfo
  std::set<int32_t> related_npu_ids = CollectRelatedNpuIds(phy_dev_id, topo_data);
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  ret = BuildNpuRootinfos(related_npu_ids, is_server, npu_rootinfos);
  if (ret != SUCCESS) {
    return ret;
  }

  std::string plane_pg_0_eid, plane_pg_1_eid;
  int mesh_die_id = 0;
  CollectClosPgEids(npu_rootinfos, phy_dev_id, is_server, plane_pg_0_eid, plane_pg_1_eid, mesh_die_id);

  // 5. 生成所有边
  std::vector<EndpointConfig> all_edges;
  CollectAllEdges(topo_data, route_data, npu_rootinfos, phy_dev_id, plane_pg_0_eid, plane_pg_1_eid, all_edges);
  if (all_edges.empty()) {
    return PARAM_INVALID;
  }

  // 6. 获取 net_instance_id 并组装结果
  std::string net_instance_id;
  ret = GetClosNetInstanceId(phy_dev_id, net_instance_id);
  if (ret != SUCCESS) {
    return ret;
  }

  local_comm_res.version = "1.3";
  local_comm_res.net_instance_id = net_instance_id;
  local_comm_res.endpoint_list = std::move(all_edges);
  for (auto &ep : local_comm_res.endpoint_list) {
    ep.net_instance_id = net_instance_id;
  }

  HIXL_LOGI("GenerateLocalCommRes result: version=%s, net_instance_id=%s, endpoints=%zu",
            local_comm_res.version.c_str(), local_comm_res.net_instance_id.c_str(),
            local_comm_res.endpoint_list.size());
  LogEndpointList(local_comm_res.endpoint_list);
  return SUCCESS;
}

}  // namespace hixl
