#include <string>
#include "common/rank_table_generator.h"

extern "C" {
    std::string GetIpAddressFromHccnTool(uint32_t phy_device_id) {
        std::string command = "";
        return GetHccnOutput(command);
    }

    std::string GetHccnOutput(std::string command) {
        return "ipaddr:127.0.0.1";
    }

    int32_t mmRealPath(const char *path, char *realPath, int32_t realPathLen) {
        return 1;
    }
}