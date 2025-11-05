#include "common/rank_table_generator.h"
#include <gtest/gtest.h>


TEST(DeviceIpTest, TestDeviceIp) {
    llm::LocalCommResGenerator generator;
    std::string ip;

    auto ret = generator.GetDeviceIp(0, ip);
    EXPECT_EQ(ret, ge::SUCCESS);
    EXPECT_EQ(ip, "127.0.0.1");
}