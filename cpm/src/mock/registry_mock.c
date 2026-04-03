/*
 * cpm — Mock Registry Implementation
 * Simulates a real registry without network for demos and tests.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock/registry_mock.h"
#include "core/utils.h"

static const char *mock_packages[] = {
    "libcurl", "nlohmann-json", "cJSON", "libuv", "zlib", "openssl",
    "googletest", "cmocka", "fmt", "spdlog", "catch2", NULL
};

static const char *mock_versions[] = {
    "libcurl:8.4.0,8.3.0,8.2.0,8.0.0",
    "nlohmann-json:3.11.3,3.11.2,3.11.0,3.10.0",
    "cJSON:1.7.17,1.7.16,1.7.15",
    "libuv:2.0.0,1.46.0,1.45.0",
    "zlib:1.3.1,1.3.0,1.2.13",
    "openssl:3.1.4,3.1.3,3.0.0,1.1.1",
    "googletest:1.14.0,1.13.0,1.12.0",
    "cmocka:1.1.7,1.1.6,1.1.5",
    "fmt:10.1.0,10.0.0,9.1.0",
    "spdlog:1.12.0,1.11.0,1.10.0",
    "catch2:3.4.0,3.3.0,3.2.0",
    NULL
};

static const char *mock_deps[] = {
    "libcurl:8.4.0:zlib,openssl",
    "libcurl:8.0.0:zlib",
    "nlohmann-json:3.11.3:",
    "cJSON:1.7.17:",
    "libuv:2.0.0:",
    "zlib:1.3.1:",
    "openssl:3.1.4:",
    "googletest:1.14.0:",
    "cmocka:1.1.7:",
    "fmt:10.1.0:",
    "spdlog:1.12.0:fmt",
    "catch2:3.4.0:",
    NULL
};

static const char *mock_langs[] = {
    "libcurl:c", "nlohmann-json:c++", "cJSON:c", "libuv:c", "zlib:c",
    "openssl:c", "googletest:c++", "cmocka:c", "fmt:c++", "spdlog:c++",
    "catch2:c++", NULL
};

static const char *mock_libtypes[] = {
    "libcurl:source", "nlohmann-json:header-only", "cJSON:source",
    "libuv:source", "zlib:source", "openssl:system", "googletest:source",
    "cmocka:source", "fmt:header-only", "spdlog:header-only",
    "catch2:header-only", NULL
};

int mock_registry_exists(const char *name) {
    for (int i = 0; mock_packages[i]; i++)
        if (strcmp(mock_packages[i], name) == 0) return 1;
    return 0;
}

const char *mock_registry_get_version(const char *name, const char *constraint) {
    for (int i = 0; mock_versions[i]; i++) {
        size_t nlen = strlen(name);
        if (strncmp(mock_versions[i], name, nlen) == 0 && mock_versions[i][nlen] == ':') {
            const char *vers = mock_versions[i] + nlen + 1;
            char *copy = strdup(vers);
            char *tok = strtok(copy, ",");
            static char result[32];
            result[0] = '\0';
            while (tok) {
                if (cpm_version_satisfies(tok, constraint)) {
                    if (result[0] == '\0' || cpm_compare_versions(tok, result) > 0)
                        strncpy(result, tok, sizeof(result) - 1);
                }
                tok = strtok(NULL, ",");
            }
            free(copy);
            return result[0] ? result : NULL;
        }
    }
    return NULL;
}

const char *mock_registry_get_deps(const char *name, const char *version) {
    for (int i = 0; mock_deps[i]; i++) {
        char key[128];
        snprintf(key, sizeof(key), "%s:%s:", name, version);
        if (strncmp(mock_deps[i], key, strlen(key)) == 0)
            return mock_deps[i] + strlen(key);
    }
    return "";
}

const char *mock_registry_get_lang(const char *name) {
    for (int i = 0; mock_langs[i]; i++) {
        size_t n = strlen(name);
        if (strncmp(mock_langs[i], name, n) == 0 && mock_langs[i][n] == ':')
            return mock_langs[i] + n + 1;
    }
    return "c";
}

const char *mock_registry_get_libtype(const char *name) {
    for (int i = 0; mock_libtypes[i]; i++) {
        size_t n = strlen(name);
        if (strncmp(mock_libtypes[i], name, n) == 0 && mock_libtypes[i][n] == ':')
            return mock_libtypes[i] + n + 1;
    }
    return "source";
}
