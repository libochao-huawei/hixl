/**
 * @file local_comm_res_ut.cc
 * @brief LocalCommRes 模块单元测试
 *
 * 测试覆盖：
 * - ParseTopoFile / ParseRouteFile 纯文件解析
 * - GenerateH2DEdges / GenerateD2HEdges / GenerateD2DEdges 边生成
 * - GenerateLocalCommRes 集成路径（通过 DCMI 桩函数）
 */

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include "local_comm_res_tool.h"
#include "test_mmpa_utils.h"

// DCMI 桩函数控制接口（定义在 tests/depends/dcmi/src/dcmi_stub.cc）
extern "C" {
void DcmiStubSetInitRet(int ret);
void DcmiStubSetMainboardId(unsigned int id, int ret);
void DcmiStubSetLogicId(unsigned int id, int ret);
void DcmiStubSetUrmaDeviceCnt(unsigned int cnt, int ret);
void DcmiStubSetSuperPodId(unsigned int id, int ret);
void DcmiStubSetEidCount(int count);
}

namespace hixl {
namespace test {

namespace {

// 获取测试数据目录（通过 CMake 传入）
std::string GetTestDataDir() {
#ifdef HIXL_TEST_SRC_DIR
    return std::string(HIXL_TEST_SRC_DIR) + "/engine/";
#else
    return "./";
#endif
}

// 从文件读取内容
std::string ReadFileContent(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// 重置 DCMI 桩到默认成功状态
void ResetDcmiStub() {
    DcmiStubSetInitRet(0);
    DcmiStubSetMainboardId(0x3, 0);       // Pod1
    DcmiStubSetLogicId(0, 0);
    DcmiStubSetUrmaDeviceCnt(1, 0);
    DcmiStubSetSuperPodId(0, 0);
    DcmiStubSetEidCount(2);               // 默认返回 2 个 EID
}

}  // anonymous namespace

// ============================================================================
// 纯数据驱动测试（无需 DCMI 桩）
// ============================================================================

class LocalCommResParseTest : public ::testing::Test {
protected:
    void SetUp() override {
        data_dir_ = GetTestDataDir();
    }
    std::string data_dir_;
};

// --- ParseTopoFile ---

TEST_F(LocalCommResParseTest, ParseTopoFileSuccess) {
    std::string topo_path = data_dir_ + "server_8p_noroce.json";
    TopoData topo_data;
    int32_t ret = ParseTopoFile(topo_path, topo_data);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(topo_data.links.size(), 52U);
    // 验证第一条 link
    EXPECT_EQ(topo_data.links[0].net_layer, 0);
    EXPECT_EQ(topo_data.links[0].link_type, "PEER2PEER");
    EXPECT_EQ(topo_data.links[0].topo_type, "1DMESH");
    EXPECT_EQ(topo_data.links[0].local_a, 0);
    EXPECT_EQ(topo_data.links[0].local_b, 1);
}

TEST_F(LocalCommResParseTest, ParseTopoFileNotFound) {
    TopoData topo_data;
    int32_t ret = ParseTopoFile("/nonexistent/path/topo.json", topo_data);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);
}

TEST_F(LocalCommResParseTest, ParseTopoFileInvalidJson) {
    std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", "not valid json {{{");
    ASSERT_FALSE(tmp.empty());
    TopoData topo_data;
    int32_t ret = ParseTopoFile(tmp, topo_data);
    EXPECT_NE(ret, SUCCESS);
    unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileEmptyEdgeList) {
    std::string json = R"({"version":"2.0","peer_count":0,"peer_list":[],"edge_count":0,"edge_list":[]})";
    std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", json);
    ASSERT_FALSE(tmp.empty());
    TopoData topo_data;
    int32_t ret = ParseTopoFile(tmp, topo_data);
    // ParseTopoFile 将空 edge_list 视为解析失败
    EXPECT_EQ(ret, ERROR_FILE_PARSE_FAILED);
    unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileEmptyContent) {
    // 空文件内容 → ERROR_FILE_PARSE_FAILED
    std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", "");
    ASSERT_FALSE(tmp.empty());
    TopoData topo_data;
    int32_t ret = ParseTopoFile(tmp, topo_data);
    EXPECT_EQ(ret, ERROR_FILE_PARSE_FAILED);
    unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileMissingNetLayer) {
    // edge 对象缺少 net_layer 字段 → 该 edge 被跳过，links 为空
    std::string json = R"({"version":"2.0","edge_list":[{"link_type":"PEER2PEER","topo_type":"1DMESH","local_a":0,"local_b":1}]})";
    std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", json);
    ASSERT_FALSE(tmp.empty());
    TopoData topo_data;
    int32_t ret = ParseTopoFile(tmp, topo_data);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_TRUE(topo_data.links.empty());
    unlink(tmp.c_str());
}

// --- ParseRouteFile ---

TEST_F(LocalCommResParseTest, ParseRouteFileSuccess) {
    std::string route_path = data_dir_ + "route.conf";
    RouteData route_data;
    int32_t ret = ParseRouteFile(route_path, route_data);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(route_data.entries.size(), 8U);
    // 验证第一条 entry
    EXPECT_EQ(route_data.entries[0].device_id, 0);
    EXPECT_FALSE(route_data.entries[0].local_eid.empty());
    EXPECT_FALSE(route_data.entries[0].remote_eid.empty());
}

TEST_F(LocalCommResParseTest, ParseRouteFileNotFound) {
    RouteData route_data;
    int32_t ret = ParseRouteFile("/nonexistent/path/route.conf", route_data);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);
}

TEST_F(LocalCommResParseTest, ParseRouteFileMalformed) {
    std::string content = "pair_device_num=1\npair0_dev_id=0\n";  // 缺少 chan 信息
    std::string tmp = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", content);
    ASSERT_FALSE(tmp.empty());
    RouteData route_data;
    int32_t ret = ParseRouteFile(tmp, route_data);
    // pair_device_num 存在但 chan 信息不全，解析应成功但 entries 可能为空
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_TRUE(route_data.entries.empty());
    unlink(tmp.c_str());
}

// ============================================================================
// 边生成测试（纯数据结构操作，无需 DCMI）
// ============================================================================

class LocalCommResEdgeTest : public ::testing::Test {};

// --- GenerateH2DEdges ---

TEST_F(LocalCommResEdgeTest, GenerateH2DEdgesSuccess) {
    RouteData route_data;
    RouteEntry e1;
    e1.device_id = 0;
    e1.local_eid = "0xaa";
    e1.remote_eid = "0xbb";
    route_data.entries.push_back(e1);

    RouteEntry e2;
    e2.device_id = 1;
    e2.local_eid = "0xcc";
    e2.remote_eid = "0xdd";
    route_data.entries.push_back(e2);

    std::vector<EndpointConfig> edges;
    int32_t ret = GenerateH2DEdges(route_data, edges);
    EXPECT_EQ(ret, SUCCESS);
    ASSERT_EQ(edges.size(), 2U);
    EXPECT_EQ(edges[0].protocol, "ub_ctp");
    EXPECT_EQ(edges[0].comm_id, "0xaa");
    EXPECT_EQ(edges[0].placement, "host");
    EXPECT_EQ(edges[0].dst_eid, "0xbb");
    EXPECT_EQ(edges[1].comm_id, "0xcc");
    EXPECT_EQ(edges[1].dst_eid, "0xdd");
}

TEST_F(LocalCommResEdgeTest, GenerateH2DEdgesEmptyRoute) {
    RouteData route_data;
    std::vector<EndpointConfig> edges;
    int32_t ret = GenerateH2DEdges(route_data, edges);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_TRUE(edges.empty());
}

// --- GenerateD2HEdges ---

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesSuccess) {
    RouteData route_data;
    RouteEntry e1;
    e1.device_id = 0;
    e1.local_eid = "0xaa";
    e1.remote_eid = "0xbb";
    route_data.entries.push_back(e1);

    std::vector<EndpointConfig> edges;
    int32_t ret = GenerateD2HEdges(route_data, edges);
    EXPECT_EQ(ret, SUCCESS);
    ASSERT_EQ(edges.size(), 1U);
    EXPECT_EQ(edges[0].protocol, "ub_ctp");
    EXPECT_EQ(edges[0].comm_id, "0xbb");   // D2H: comm_id = remote_eid
    EXPECT_EQ(edges[0].placement, "device");
    EXPECT_EQ(edges[0].dst_eid, "0xaa");   // D2H: dst_eid = local_eid
}

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesEmptyRoute) {
    RouteData route_data;
    std::vector<EndpointConfig> edges;
    int32_t ret = GenerateD2HEdges(route_data, edges);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_TRUE(edges.empty());
}

// Note: GenerateD2UEdges / GenerateH2UEdges 在 .cc 中的实际签名与 .h 声明不一致，
// 无法从 UT 直接调用，通过 GenerateLocalCommRes 集成路径间接覆盖。

// --- GenerateD2DEdges ---

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesEmptyTopo) {
    TopoData topo_data;
    std::map<int32_t, NpuRootInfo> npu_rootinfos;
    std::vector<EndpointConfig> edges;
    int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_TRUE(edges.empty());
}

// ============================================================================
// GenerateLocalCommRes 集成测试（需要 DCMI 桩）
// ============================================================================

class LocalCommResGenerateTest : public ::testing::Test {
protected:
    void SetUp() override {
        ResetDcmiStub();
        data_dir_ = GetTestDataDir();
    }

    void TearDown() override {
        ResetDcmiStub();
    }

    std::string data_dir_;
};

TEST_F(LocalCommResGenerateTest, GenerateSuccess) {
    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(res.version, "1.3");
    EXPECT_FALSE(res.endpoint_list.empty());
    // 所有 endpoint 应有 net_instance_id
    for (const auto& ep : res.endpoint_list) {
        EXPECT_FALSE(ep.net_instance_id.empty());
    }
}

TEST_F(LocalCommResGenerateTest, GenerateTopoNotFound) {
    std::map<std::string, std::string> options;
    options["topo_path"] = "/nonexistent/topo.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);
}

TEST_F(LocalCommResGenerateTest, GenerateRouteNotFound) {
    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = "/nonexistent/route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);
}

TEST_F(LocalCommResGenerateTest, GenerateGetMainboardIdFailed) {
    DcmiStubSetMainboardId(0, -1);  // 模拟失败

    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateGetClosNetInstanceIdFailed) {
    DcmiStubSetSuperPodId(0, -1);  // 模拟 SPOD 查询失败

    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateDefaultTopoPathNotFound) {
    // 不指定 topo_path，且 /etc/ 下无 noroce.json → 应返回 ERROR_FILE_NOT_FOUND
    // 注意：如果测试环境恰好有 /etc/*noroce.json，此测试可能失败
    std::map<std::string, std::string> options;
    options["topo_path"] = "/tmp/nonexistent_dir_for_test_xxxx/no_match.json";
    options["route_path"] = data_dir_ + "route.conf";

    // 直接指定一个不存在的路径来模拟默认路径找不到的情况
    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);
}

TEST_F(LocalCommResGenerateTest, GeneratePodMainboardId) {
    DcmiStubSetMainboardId(0x3, 0);  // Pod1

    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GenerateServerMainboardId) {
    DcmiStubSetMainboardId(0x21, 0);  // Server

    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateBuildNpuRootinfosFailed) {
    // URMA 设备数为 0 → BuildNpuRootInfo 返回 ERROR_NO_EID_FOUND → BuildNpuRootinfos 失败
    DcmiStubSetUrmaDeviceCnt(0, 0);

    std::map<std::string, std::string> options;
    options["topo_path"] = data_dir_ + "server_8p_noroce.json";
    options["route_path"] = data_dir_ + "route.conf";

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, ERROR_NO_EID_FOUND);
}

TEST_F(LocalCommResGenerateTest, GenerateEmptyAllEdges) {
    // topo 中无匹配 link（net_layer=1），route entries 为空，DCMI 仅返回非 PG EID → 所有边为空
    DcmiStubSetEidCount(1);  // 仅返回非 PG EID（byte6=0xf2），plane_pg EID 为空

    std::string topo_json = R"({"version":"2.0","edge_list":[{"net_layer":1,"link_type":"PEER2PEER","topo_type":"1DMESH","local_a":0,"local_b":1}]})";
    std::string tmp_topo = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", topo_json);
    ASSERT_FALSE(tmp_topo.empty());

    std::string tmp_route = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", "pair_device_num=0\n");
    ASSERT_FALSE(tmp_route.empty());

    std::map<std::string, std::string> options;
    options["topo_path"] = tmp_topo;
    options["route_path"] = tmp_route;

    LocalCommRes res;
    int32_t ret = GenerateLocalCommRes(0, options, res);
    EXPECT_EQ(ret, ERROR_FILE_NOT_FOUND);

    unlink(tmp_topo.c_str());
    unlink(tmp_route.c_str());
}

}  // namespace test
}  // namespace hixl
