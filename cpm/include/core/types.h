/*
 * cpm — Core Type Definitions
 */

#ifndef CPM_CORE_TYPES_H
#define CPM_CORE_TYPES_H

#include <stddef.h>

#define CPM_VERSION     "0.1.0"
#define DEFAULT_REGISTRY "https://registry.cpm.dev"
#define CPM_DIR          ".cpm"
#define CPM_DEPS_DIR     ".cpm/deps"
#define CPM_CACHE_DIR    "~/.cpm"

typedef struct {
    char *name;
    char *version;
    char *source;
    char *checksum;
    char **deps;
    size_t dep_count;
} lock_package_t;

typedef struct {
    lock_package_t *packages;
    size_t count;
} lockfile_t;

#endif /* CPM_CORE_TYPES_H */
