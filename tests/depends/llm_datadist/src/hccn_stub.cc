#include <string>

extern "C" {
    std::string GetHccnOutput(std::string command) {
        return "ipaddr:127.0.0.1";
    }

    int32_t mmRealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) {
        return 1;
    }
}