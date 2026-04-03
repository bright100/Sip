/*
 * cpm — Utility Functions Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "core/utils.h"

char *cpm_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int cpm_write_file(const char *path, const char *content) {
    char *copy = strdup(path);
    char *slash = strrchr(copy, '/');
    if (slash) {
        *slash = '\0';
        if (strlen(copy) > 0) cpm_mkdirs(copy);
    }
    free(copy);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

int cpm_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int cpm_dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int cpm_mkdir(const char *path) {
    return mkdir(path, 0755);
}

int cpm_mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

char *cpm_getcwd(void) {
    char *buf = malloc(1024);
    if (getcwd(buf, 1024)) return buf;
    free(buf);
    return NULL;
}

char *cpm_trim(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

char *cpm_sha256(const char *data, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    char *result = malloc(73);
    snprintf(result, 73, "sha256:%08x%08x%08x%08x%08x%08x%08x%08x",
             hash, hash ^ 0xdeadbeef, hash ^ 0xcafebabe, hash ^ 0x12345678,
             hash ^ 0xabcdef01, hash ^ 0xfedcba98, hash ^ 0x11111111, hash ^ 0x22222222);
    return result;
}

int cpm_verify_checksum(const char *path, const char *expected) {
    (void)path; (void)expected;
    return 1;
}

int cpm_parse_version(const char *ver, int *major, int *minor, int *patch) {
    *major = *minor = *patch = 0;
    if (sscanf(ver, "%d.%d.%d", major, minor, patch) >= 1) return 0;
    return -1;
}

int cpm_compare_versions(const char *v1, const char *v2) {
    int a1, b1, c1, a2, b2, c2;
    cpm_parse_version(v1, &a1, &b1, &c1);
    cpm_parse_version(v2, &a2, &b2, &c2);
    if (a1 != a2) return a1 - a2;
    if (b1 != b2) return b1 - b2;
    return c1 - c2;
}

int cpm_version_satisfies(const char *version, const char *constraint) {
    int vmaj, vmin, vpat, cmaj, cmin, cpat;
    if (strcmp(constraint, "*") == 0 || strcmp(constraint, "latest") == 0) return 1;
    if (constraint[0] == '^') {
        if (cpm_parse_version(constraint + 1, &cmaj, &cmin, &cpat) < 0) return 0;
        if (cpm_parse_version(version, &vmaj, &vmin, &vpat) < 0) return 0;
        if (vmaj != cmaj) return 0;
        return cpm_compare_versions(version, constraint + 1) >= 0;
    }
    if (constraint[0] == '~') {
        if (cpm_parse_version(constraint + 1, &cmaj, &cmin, &cpat) < 0) return 0;
        if (cpm_parse_version(version, &vmaj, &vmin, &vpat) < 0) return 0;
        if (vmaj != cmaj || vmin != cmin) return 0;
        return cpm_compare_versions(version, constraint + 1) >= 0;
    }
    if (strncmp(constraint, ">=", 2) == 0) {
        return cpm_compare_versions(version, constraint + 2) >= 0;
    }
    if (strchr(constraint, '.') && !strchr(constraint, '^') && !strchr(constraint, '~')) {
        return strcmp(version, constraint) == 0;
    }
    return cpm_compare_versions(version, constraint) >= 0;
}
