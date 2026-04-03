/*
 * cpm — Utility Function Declarations
 */

#ifndef CPM_CORE_UTILS_H
#define CPM_CORE_UTILS_H

#include <stddef.h>

char *cpm_read_file(const char *path);
int   cpm_write_file(const char *path, const char *content);
int   cpm_file_exists(const char *path);
int   cpm_dir_exists(const char *path);
int   cpm_mkdir(const char *path);
int   cpm_mkdirs(const char *path);
char *cpm_getcwd(void);
char *cpm_trim(char *str);
char *cpm_sha256(const char *data, size_t len);
int   cpm_verify_checksum(const char *path, const char *expected);
int   cpm_parse_version(const char *ver, int *major, int *minor, int *patch);
int   cpm_compare_versions(const char *v1, const char *v2);
int   cpm_version_satisfies(const char *version, const char *constraint);

#endif /* CPM_CORE_UTILS_H */
