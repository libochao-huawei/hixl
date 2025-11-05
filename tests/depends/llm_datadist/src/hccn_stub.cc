#include <string>
#include "common/rank_table_generator.h"

extern "C" {
    char *fgets(char *str, int n, FILE *stream) {
        const char *ip = "ipaddr:127.0.0.1";
        size_t len = strlen(ip);
        n = len + 1;
        strncpy(str, ip, len);
        str[len] = '\0';
        return str;
    }
    

    int32_t mmRealPath(const char *path, char *realPath, int32_t realPathLen) {
        return 1;
    }
}